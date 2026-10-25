#include "FWM/Camera.h"

#include "FWM/Enemies.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/Settings.h"

#define SMOOTHCAM_API_COMMONLIB
#include "SmoothCamAPI.h"

namespace FWM::Camera
{
	namespace
	{
		std::atomic<RE::FormID> focus{ 0 };
		std::atomic<bool>       portraitWanted{ false };  // no fight on: face to face
		std::atomic<bool>       overheadWanted{ false };
		std::atomic<bool>       driving{ false };
        std::atomic<float> treeLift{0.f};
        bool treeOnly{false}, treePoseSaved{false};

		// Turns and zoom asked for since the last frame: the right stick, a right-drag, the wheel.
		std::atomic<float> yawIn{ 0.0f }, pitchIn{ 0.0f }, zoomIn{ 0.0f };
		// Free move overhead, in screen heights: a middle-drag, the left stick, WASD.
		std::atomic<float> panRightIn{ 0.0f }, panForwardIn{ 0.0f };
		std::atomic<bool>  recentreIn{ false };

		// Seconds for each kind of move. Every move is a timed ease-in-out from wherever
		// the camera is shown to where it should be, so nothing ever jumps or lurches.
		constexpr float kOpenSeconds = 0.95f;
		constexpr float kSwitchSeconds = 0.85f;
		constexpr float kCloseSeconds = 0.7f;
		constexpr float kArc = 45.0f;  // a switch rises a little through its middle

		// Overhead takes in everyone fighting within this distance of the player.
		constexpr float kOverheadReach = 4500.0f;

		struct Pose
		{
			RE::NiPoint3 eye{};
			RE::NiPoint3 look{};
		};

		bool active{ false };   // we write the camera this frame
		bool closing{ false };  // easing home, then letting go
		bool forcedThird{ false };

		// Home: the view on screen when the plan opened, kept relative to the player.
		RE::NiPoint3 homeOffset{};
		RE::NiPoint3 homeForward{ 0.0f, 1.0f, 0.0f };

		Pose treePose{};
        Pose  shown{};  // what was written last frame
		Pose  from{};   // where the current move started
		float progress{ 1.0f };
		float duration{ kOpenSeconds };
		float arc{ 0.0f };

		// Chosen once per companion, then held, so the camera never swings while it follows.
		RE::FormID   subject{ 0 };
		float        eyeHeight{ 120.0f };
		RE::NiPoint3 bearing{ 0.0f, 1.0f, 0.0f };
		float        side{ 1.0f };
		bool         portraitShown{ false };

		// The player's turn of the view around the chosen companion, on top of the held
		// approach. A new companion is approached from where the view already is, so the
		// turn folds into the approach and starts again from nothing.
		float orbitYaw{ 0.0f };
		float orbitPitch{ 0.0f };
		float zoom{ 1.0f };
		float steering{ 0.0f };  // the view follows quickly for a moment after a turn

		// Overhead: the fight's middle and size, followed gently so the view does not
		// wander as people move, and the direction it is seen from.
		bool         overheadShown{ false };
		bool         overheadAimed{ false };  // tilted from the INI once a session; the player's tilt after that
		float        overheadYaw{ 0.0f };
		float        overheadPitch{ 1.2f };
		float        overheadZoom{ 1.0f };
		RE::NiPoint3 fightCentre{};
		float        fightRadius{ 800.0f };
		RE::NiPoint3 panOffset{};  // how far the player has slid the view off the fight
		float        panRight{ 0.0f }, panForward{ 0.0f };  // this frame's slide, for Overview

		// Smart cutaway: whether anything stands between the fighters and the camera above them
		// (a cave roof, a ceiling, an overhang). Checked a few times a second, and changed only on
		// a clear majority so it never flickers.
		bool  covered{ false };
		float coverCheckIn{ 0.0f };

		// The cutaway: under cover, overhead, nothing nearer the camera than this height is drawn.
		float cutZ{ 0.0f };
		bool  cutKnown{ false };
		bool  nearSaved{ false };
		float nearOriginal{ 15.0f };

		SmoothCamAPI::IVSmoothCam2* smoothCam{ nullptr };
		bool                        smoothCamHeld{ false };
		bool                        smoothCamRegistered{ false };

		RE::NiPoint3 Lerp(const RE::NiPoint3& a, const RE::NiPoint3& b, float t) { return a + (b - a) * t; }

		RE::NiPoint3 Follow(const RE::NiPoint3& a_current, const RE::NiPoint3& a_target, float a_dt, float a_rate)
		{
			return a_target + (a_current - a_target) * std::exp(-a_rate * a_dt);
		}

		float EaseInOut(float t)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
		}

		RE::NiPoint3 Forward(const RE::NiMatrix3& m) { return { m.entry[0][1], m.entry[1][1], m.entry[2][1] }; }

		void Move(float a_seconds, float a_arc)
		{
			from = shown;
			progress = 0.0f;
			duration = a_seconds;
			arc = a_arc;
		}

		bool AcquireSmoothCam()
		{
			if (smoothCamHeld || !smoothCam) {
				smoothCamHeld = true;
				return true;
			}
			const auto result = smoothCam->RequestCameraControl(SKSE::GetPluginHandle());
			if (result == SmoothCamAPI::APIResult::OK || result == SmoothCamAPI::APIResult::AlreadyGiven) {
				smoothCamHeld = true;
				return true;
			}
			Log::Info("[Camera] SmoothCam is held by another plugin; no glide this time."sv);
			return false;
		}

		void ReleaseSmoothCam()
		{
			if (!smoothCamHeld) {
				return;
			}
			smoothCamHeld = false;
			if (smoothCam) {
				smoothCam->ReleaseCameraControl(SKSE::GetPluginHandle());
			}
		}

		// Pulls the camera in front of anything between it and the subject. The ray starts at the
		// subject's face, so it must not see the subject: drawn weapons, a shield or a staff
		// held up sit right in front of the face, and hitting them put the camera on the nose.
		// Sharing the subject's collision group makes the ray pass through all of their parts.
		RE::NiPoint3 Clear(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, RE::Actor* a_subject)
		{
			auto*       player = RE::PlayerCharacter::GetSingleton();
			auto*       cell = player ? player->GetParentCell() : nullptr;
			auto*       world = cell ? cell->GetbhkWorld() : nullptr;
			const float scale = RE::bhkWorld::GetWorldScale();
			if (!world || !(scale > 0.0f)) {
				return a_to;
			}
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4{ a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f };
			pick.rayInput.to = RE::hkVector4{ a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f };
			RE::CFilter own{};
			if (a_subject) {
				a_subject->GetCollisionFilterInfo(own);
			}
			pick.rayInput.filterInfo.filter = static_cast<std::uint32_t>(RE::COL_LAYER::kLOS) | (own.GetSystemGroup() ? own.GetSystemGroup() << 16 : 1u << 16);
			world->PickObject(pick);
			if (pick.pickFailed || !pick.rayOutput.HasHit()) {
				return a_to;
			}
			// Never closer than 45 units, whatever is in the way: a wall at the cheek is better
			// than a lens on the nose.
			const float fraction = std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f);
			const float length = (a_to - a_from).Length();
			const float nearest = length > 1.0f ? std::min(45.0f / length, 1.0f) : 1.0f;
			return Lerp(a_from, a_to, std::max(fraction - 0.08f, nearest));
		}

		// Whether anything solid lies between two points: walls, rock, roofs, trees.
		bool Blocked(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
		{
			auto*       player = RE::PlayerCharacter::GetSingleton();
			auto*       cell = player ? player->GetParentCell() : nullptr;
			auto*       world = cell ? cell->GetbhkWorld() : nullptr;
			const float scale = RE::bhkWorld::GetWorldScale();
			if (!world || !(scale > 0.0f)) {
				return false;
			}
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4{ a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f };
			pick.rayInput.to = RE::hkVector4{ a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f };
			pick.rayInput.filterInfo.filter = static_cast<std::uint32_t>(RE::COL_LAYER::kLOS) | (1u << 16);
			world->PickObject(pick);
			return !pick.pickFailed && pick.rayOutput.HasHit();
		}

		void Write(RE::NiNode* a_root, const Pose& a_pose, float a_dt)
		{
			RE::NiPoint3 forward = a_pose.look - a_pose.eye;
			const float  length = forward.Length();
			if (!(length > 0.01f)) {
				return;
			}
			forward = forward / length;
			RE::NiPoint3 right = forward.Cross({ 0.0f, 0.0f, 1.0f });
			const float  rightLength = right.Length();
			if (!(rightLength > 0.0001f)) {
				return;
			}
			right = right / rightLength;
			const RE::NiPoint3 up = right.Cross(forward);

			// Columns are right / forward / up, as the engine's own camera matrix.
			RE::NiMatrix3 rotation{};
			rotation.entry[0][0] = right.x;
			rotation.entry[0][1] = forward.x;
			rotation.entry[0][2] = up.x;
			rotation.entry[1][0] = right.y;
			rotation.entry[1][1] = forward.y;
			rotation.entry[1][2] = up.y;
			rotation.entry[2][0] = right.z;
			rotation.entry[2][1] = forward.z;
			rotation.entry[2][2] = up.z;

			a_root->world.translate = a_pose.eye;
			a_root->world.rotate = rotation;
			a_root->local.translate = a_pose.eye;
			a_root->local.rotate = rotation;
			RE::NiUpdateData update{};
			update.time = a_dt;
			update.flags = static_cast<RE::NiUpdateData::Flag>(0x2000);
			a_root->Update(update);
		}

		Pose Home()
		{
			auto*        player = RE::PlayerCharacter::GetSingleton();
			const auto   origin = player ? player->GetPosition() : RE::NiPoint3{};
			const auto   eye = origin + homeOffset;
			return { eye, eye + homeForward * 400.0f };
		}

		// Where the camera should be to frame this companion. Picks the approach the
		// first time a companion is framed and holds it; the player's turn goes on top.
		std::optional<Pose> Framing(RE::Actor* a_actor, bool a_portrait)
		{
			if (!a_actor || !a_actor->Is3DLoaded() || a_actor->IsDisabled()) {
				return std::nullopt;
			}
			const auto ground = a_actor->GetPosition();
			if (a_actor->GetFormID() != subject) {
				// Sample the eye height once, never per frame: the head bone breathes and sways.
				eyeHeight = 120.0f;
				if (auto* model = a_actor->Get3D()) {
					if (auto* head = model->GetObjectByName("NPC Head [Head]")) {
						eyeHeight = std::clamp(head->world.translate.z - ground.z, 40.0f, 400.0f);
					}
				}
				// Come at them from where the camera already is, so the move reads as a glide.
				RE::NiPoint3 flat{ ground.x - shown.eye.x, ground.y - shown.eye.y, 0.0f };
				const float  flatLength = flat.Length();
				bearing = flatLength > 1.0f ? flat / flatLength : RE::NiPoint3{ 0.0f, 1.0f, 0.0f };
				subject = a_actor->GetFormID();
				orbitYaw = 0.0f;
			}
			const auto&        settings = Settings::Get();
			const RE::NiPoint3 anchor{ ground.x, ground.y, ground.z + eyeHeight };
			if (a_portrait) {
				// Face to face, close, at eye height: in front of where they are turned.
				const float        heading = a_actor->GetAngleZ() + orbitYaw;
				const RE::NiPoint3 facing{ std::sin(heading), std::cos(heading), 0.0f };
				const RE::NiPoint3 face{ anchor.x, anchor.y, anchor.z - 4.0f };
				const float        rise = std::clamp(orbitPitch, -0.6f, 1.1f);
				const float        reach = settings.portraitDistance * zoom;
				RE::NiPoint3       place = face + facing * (reach * std::cos(rise));
				place.z += 3.0f + reach * std::sin(rise);
				return Pose{ Clear(face, place, a_actor), face };
			}
			// The held approach is the same pose as distance and height straight back and up;
			// the turn swings it around the companion and tilts it over them.
			const float        yaw = std::atan2(bearing.x, bearing.y) + orbitYaw;
			const RE::NiPoint3 along{ std::sin(yaw), std::cos(yaw), 0.0f };
			const RE::NiPoint3 right{ along.y, -along.x, 0.0f };
			const float        reach = std::sqrt(settings.cameraDistance * settings.cameraDistance + settings.cameraHeight * settings.cameraHeight) * zoom;
			const float        rise = std::clamp(std::atan2(settings.cameraHeight, settings.cameraDistance) + orbitPitch, -0.35f, 1.35f);
			RE::NiPoint3       place = anchor - along * (reach * std::cos(rise)) + right * (side * 45.0f);
			place.z += reach * std::sin(rise);
			return Pose{ Clear(anchor, place, a_actor), { anchor.x, anchor.y, anchor.z - 18.0f } };
		}

		// The whole fight from above: everyone in it within reach of the player, framed with
		// room to spare and a little left of centre, where the companion's card is not.
		std::optional<Pose> Overview(float a_dt, bool a_first)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Is3DLoaded()) {
				return std::nullopt;
			}
			const auto                origin = player->GetPosition();
			float                     minX = origin.x, maxX = origin.x, minY = origin.y, maxY = origin.y;
			float                     top = Game::HeadPosition(player).z;
			std::vector<RE::NiPoint3> spots{ origin };
			std::vector<RE::NiPoint3> heads{ Game::HeadPosition(player) };
			auto                      take = [&](RE::Actor* a_actor) {
				if (!a_actor || a_actor == player || !a_actor->Is3DLoaded() || a_actor->IsDisabled() || a_actor->IsDead()) {
					return;
				}
				const auto at = a_actor->GetPosition();
				if (at.GetDistance(origin) > kOverheadReach) {
					return;
				}
				spots.push_back(at);
				minX = std::min(minX, at.x);
				maxX = std::max(maxX, at.x);
				minY = std::min(minY, at.y);
				maxY = std::max(maxY, at.y);
				const auto head = Game::HeadPosition(a_actor);
				top = std::max(top, head.z);
				heads.push_back(head);
			};
			for (auto& member : Party::Members()) {
				auto actor = member.handle.get();
				take(actor.get());
			}
			for (auto& enemy : Enemies::All()) {
				auto actor = enemy.handle.get();
				take(actor.get());
			}

			const RE::NiPoint3 centre{ (minX + maxX) * 0.5f, (minY + maxY) * 0.5f, origin.z };
			float              radius = 0.0f;
			for (const auto& at : spots) {
				radius = std::max(radius, std::hypot(at.x - centre.x, at.y - centre.y));
			}
			radius = std::clamp(radius + 160.0f, 450.0f, 3500.0f);
			const auto& settings = Settings::Get();
			const float cut = top + settings.cutawayMargin;
			if (a_first || !cutKnown) {
				fightCentre = centre;
				fightRadius = radius;
				cutZ = cut;
			} else {
				const float k = 1.0f - std::exp(-1.5f * a_dt);
				fightCentre = Follow(fightCentre, centre, a_dt, 1.5f);
				fightRadius += (radius - fightRadius) * k;
				// Up at once, so nobody's head is ever clipped; down slowly.
				cutZ = cut > cutZ ? cut : cutZ + (cut - cutZ) * k;
			}
			cutKnown = true;

			// The frustum is kept at unit distance: these are the tangents of half the view.
			float tanV = 0.55f, tanH = 0.95f;
			if (auto* camera = RE::Main::WorldRootCamera()) {
				const auto& frustum = camera->GetRuntimeData2().viewFrustum;
				if (!frustum.bOrtho && frustum.fTop > 0.1f && frustum.fTop < 5.0f && frustum.fRight > 0.1f && frustum.fRight < 5.0f) {
					tanV = frustum.fTop;
					tanH = frustum.fRight;
				}
			}
			// The title and orders take the top of the screen, the strip the bottom and the card
			// the right quarter: the fight is fitted into what is left, and centred in it.
			const float        reach = std::clamp(fightRadius / std::min(tanV * 0.72f, tanH * 0.7f) * overheadZoom, 500.0f, 12000.0f);
			const RE::NiPoint3 along{ std::sin(overheadYaw), std::cos(overheadYaw), 0.0f };
			const RE::NiPoint3 right{ along.y, -along.x, 0.0f };
			// Free move: the slide asked for this frame, a screen height being the ground the view
			// spans at the fight, foreshortened along the line of sight.
			if (a_first) {
				panOffset = {};
			}
			const float span = 2.0f * reach * tanV;
			panOffset += right * (panRight * span) + along * (panForward * span / std::max(std::sin(overheadPitch), 0.3f));
			if (const float length = panOffset.Length(); length > 6000.0f) {
				panOffset *= 6000.0f / length;
			}
			panRight = panForward = 0.0f;
			const RE::NiPoint3 look = fightCentre + panOffset + right * (reach * tanH * 0.25f);
			RE::NiPoint3       eye = look - along * (reach * std::cos(overheadPitch));
			eye.z += reach * std::sin(overheadPitch);

			// Is anything in the way from above? Lines of sight from just over each fighter's head
			// to where the camera wants to be: blocked for a third or more, there is a roof; clear
			// for nine in ten, there is not; in between, as it was.
			coverCheckIn -= a_dt;
			if (a_first || coverCheckIn <= 0.0f) {
				coverCheckIn = 0.25f;
				int blocked = 0;
				for (const auto& head : heads) {
					blocked += Blocked(head + RE::NiPoint3{ 0.0f, 0.0f, 40.0f }, eye) ? 1 : 0;
				}
				const float share = heads.empty() ? 0.0f : static_cast<float>(blocked) / static_cast<float>(heads.size());
				const bool  was = covered;
				if (a_first) {
					covered = share >= 0.34f;
				} else if (share >= 0.34f) {
					covered = true;
				} else if (share <= 0.1f) {
					covered = false;
				}
				if (a_first || covered != was) {
					Log::Info("[Camera] Overhead: {} of {} lines of sight from the fighters to the camera are blocked; {}."sv, blocked, heads.size(),
						covered ? (settings.cutaway ? "cutting above their heads"sv : "coming down under the cover"sv) : "open above, nothing cut"sv);
				}
			}
			// The cutaway off and a roof overhead: come down under it.
			if (covered && !settings.cutaway) {
				eye = Clear(look, eye, nullptr);
			}
			return Pose{ eye, look };
		}

		void RestoreNear()
		{
			if (!nearSaved) {
				return;
			}
			nearSaved = false;
			if (auto* camera = RE::Main::WorldRootCamera()) {
				camera->GetRuntimeData2().viewFrustum.fNear = nearOriginal;
			}
		}

		// Indoors, looking down from above everyone's heads: the near plane moves out to where
		// the centre of the view crosses the cut, so the ceiling (and anything else that high)
		// is clipped. The engine rebuilds the frustum only when the field of view changes, so
		// the value set here holds; it is set again every frame all the same.
		bool ApplyCut(const Pose& a_pose)
		{
			auto* camera = RE::Main::WorldRootCamera();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			if (!camera || !cutKnown || !cell || !covered || !Settings::Get().cutaway) {
				RestoreNear();
				return false;
			}
			RE::NiPoint3 forward = a_pose.look - a_pose.eye;
			const float  toLook = forward.Length();
			if (!(toLook > 1.0f)) {
				RestoreNear();
				return false;
			}
			forward = forward / toLook;
			float cut = 0.0f;
			if (forward.z < -0.45f && a_pose.eye.z > cutZ + 20.0f) {
				cut = std::min((a_pose.eye.z - cutZ) / -forward.z, toLook - 80.0f);
			}
			if (cut < 25.0f) {
				RestoreNear();
				return false;
			}
			auto& frustum = camera->GetRuntimeData2().viewFrustum;
			if (!nearSaved) {
				nearOriginal = frustum.fNear;
				nearSaved = true;
			}
			frustum.fNear = cut;
			return true;
		}

		void Finish(RE::PlayerCamera* a_camera)
		{
            treeLift=0.f;treeOnly=false;treePoseSaved=false;
			active = false;
			closing = false;
			subject = 0;
			overheadShown = false;
			RestoreNear();
			cutKnown = false;
			driving = false;
			ReleaseSmoothCam();
			if (forcedThird && a_camera) {
				a_camera->ForceFirstPerson();
			}
			forcedThird = false;
		}

		void Apply()
		{
			if (!active) {
				return;
			}
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* root = camera ? camera->cameraRoot.get() : nullptr;
			if (!root) {
				return;
			}
			auto*       timer = RE::BSTimer::GetSingleton();
			const float dt = timer ? std::clamp(timer->realTimeDelta, 0.0f, 0.1f) : 0.016f;

			// The player's turns, on whichever view is up.
			const float turnYaw = yawIn.exchange(0.0f), turnPitch = pitchIn.exchange(0.0f), zoomSteps = zoomIn.exchange(0.0f);
			panRight += panRightIn.exchange(0.0f);
			panForward += panForwardIn.exchange(0.0f);
			if (recentreIn.exchange(false)) {
				panOffset = {};
				steering = 0.0f;  // glide back at the usual pace
			}
			const bool  overhead = overheadWanted.load(std::memory_order_relaxed) && !closing;
			steering = turnYaw != 0.0f || turnPitch != 0.0f || zoomSteps != 0.0f || panRight != 0.0f || panForward != 0.0f ? 0.35f :
			                                                                                                           std::max(steering - dt, 0.0f);
			if (overhead) {
				overheadYaw += turnYaw;
				overheadPitch = std::clamp(overheadPitch - turnPitch, 0.6f, 1.53f);
				overheadZoom = std::clamp(overheadZoom * std::exp(-0.12f * zoomSteps), 0.35f, 3.0f);
			} else {
				orbitYaw += turnYaw;
				orbitPitch = std::clamp(orbitPitch - turnPitch, -0.9f, 1.1f);
				zoom = std::clamp(zoom * std::exp(-0.12f * zoomSteps), 0.45f, 2.5f);
			}

			// Where the camera belongs now: the whole fight, the chosen companion, or home.
			Pose target = Home();
			if (!overhead && overheadShown) {
				// Back down to the companion, from where the view now is.
				overheadShown = false;
				subject = 0;
			}
			if (overhead) {
				const bool first = !overheadShown;
				if (first) {
					if (!overheadAimed) {
						overheadPitch = Settings::Get().overheadPitch * 0.0174533f;
						overheadAimed = true;
					}
					// Keep facing the way the view faced, so left stays left.
					const RE::NiPoint3 flat{ shown.look.x - shown.eye.x, shown.look.y - shown.eye.y, 0.0f };
					if (flat.Length() > 1.0f) {
						overheadYaw = std::atan2(flat.x, flat.y);
					}
				}
				if (const auto view = Overview(dt, first)) {
					if (first) {
						overheadShown = true;
						Move(kSwitchSeconds, 0.0f);
					}
					target = *view;
				}
			} else if (!closing && Settings::Get().cameraGlide) {
				const auto id = focus.load(std::memory_order_relaxed);
				const bool portrait = portraitWanted.load(std::memory_order_relaxed);
				const auto previous = subject;
				if (const auto framing = Framing(id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr, portrait)) {
					if (subject != previous || portrait != portraitShown) {
						// A new companion, or the fight starting or ending: glide over, on
						// alternate sides between companions.
						if (previous != 0 && subject != previous) {
							side = -side;
						}
						const bool first = previous == 0;
						Move(first ? kOpenSeconds : kSwitchSeconds, first || portrait || portraitShown ? 0.0f : kArc);
						portraitShown = portrait;
						if (const auto again = Framing(RE::TESForm::LookupByID<RE::Actor>(id), portrait)) {
							target = *again;
						}
					} else {
						target = *framing;
					}
				}
			}

			if (progress < 1.0f) {
				progress = std::min(progress + dt / duration, 1.0f);
				const float t = EaseInOut(progress);
				shown.eye = Lerp(from.eye, target.eye, t);
				shown.look = Lerp(from.look, target.look, t);
				shown.eye.z += std::sin(t * 3.14159265f) * arc;
			} else {
				// Settled: keep gently with a companion who shifts while time is slow, and
				// keep up with the player while they turn the view.
				const bool quick = steering > 0.0f;
				shown.eye = Follow(shown.eye, target.eye, dt, quick ? 16.0f : (overheadShown ? 2.5f : 3.0f));
				shown.look = Follow(shown.look, target.look, dt, quick ? 18.0f : (overheadShown ? 3.0f : 4.0f));
			}
			const float lift=treeLift.load(std::memory_order_relaxed);
            Pose output=shown;
            if(lift>0.f && !closing) {
                if(!treePoseSaved){treePose=shown;treePoseSaved=true;}
                output=treePose;
                const auto direction=treePose.look-treePose.eye;
                const float yaw=std::atan2(direction.x,direction.y);
                const float startPitch=std::atan2(direction.z,std::hypot(direction.x,direction.y));
                const float pitch=startPitch+(1.12f-startPitch)*lift;
                const float reach=std::max(direction.Length(),300.f);
                output.eye.z+=38.f*lift;
                output.look=output.eye+RE::NiPoint3{std::sin(yaw)*std::cos(pitch)*reach,std::cos(yaw)*std::cos(pitch)*reach,std::sin(pitch)*reach};
                RestoreNear();
            } else {treePoseSaved=false;ApplyCut(shown);}
            Write(root, output, dt);

			if (closing && progress >= 1.0f) {
				Finish(camera);
			}
		}

		struct ThirdPersonUpdate
		{
			static void Thunk(RE::ThirdPersonState* a_state, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
			{
				original(a_state, a_next);
				Apply();
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};
	}

	void RegisterSmoothCam()
	{
		const auto* messaging = SKSE::GetMessagingInterface();
		if (!messaging || smoothCamRegistered) {
			return;
		}
		smoothCamRegistered = SmoothCamAPI::RegisterInterfaceLoaderCallback(messaging, [](void* a_instance, SmoothCamAPI::InterfaceVersion a_version) {
			if (a_version == SmoothCamAPI::InterfaceVersion::V2 || a_version == SmoothCamAPI::InterfaceVersion::V3) {
				smoothCam = reinterpret_cast<SmoothCamAPI::IVSmoothCam2*>(a_instance);
				Log::Info("[Camera] SmoothCam found; the glide will borrow the camera from it."sv);
			}
		});
	}

	void RequestSmoothCam()
	{
		const auto* messaging = SKSE::GetMessagingInterface();
		if (messaging && smoothCamRegistered && !smoothCam) {
			(void)SmoothCamAPI::RequestInterface(messaging, SmoothCamAPI::InterfaceVersion::V2);
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_ThirdPersonState[0] };
		ThirdPersonUpdate::original = vtable.write_vfunc(0x03, ThirdPersonUpdate::Thunk);
		Log::Info("[Hook] Third-person camera installed."sv);
	}

	void Begin(bool a_treeOnly)
	{
		if (!a_treeOnly && !Settings::Get().cameraGlide) {
			return;
		}
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* root = camera ? camera->cameraRoot.get() : nullptr;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!root || !player) {
			return;
		}
		orbitYaw = 0.0f;
		orbitPitch = 0.0f;
		zoom = 1.0f;
		steering = 0.0f;
		yawIn = pitchIn = zoomIn = 0.0f;
		panRightIn = panForwardIn = 0.0f;
		panOffset = {};
		panRight = panForward = 0.0f;
		if (active && closing) {
			// Reopened while easing home: turn around from wherever the camera is.
			closing = false;
			subject = 0;
			overheadShown = false;
			driving = true;
			return;
		}
		if (active) {
			return;
		}
		// Capture the view on screen before anyone else touches the camera; every move
		// starts here and the plan comes back here when it closes.
		homeOffset = root->world.translate - player->GetPosition();
		homeForward = Forward(root->world.rotate);
		if (!AcquireSmoothCam()) {
			return;
		}
		if (camera->IsInFirstPerson()) {
			camera->ForceThirdPerson();
			forcedThird = true;
		}
		shown = Home();
		from = shown;
		progress = 1.0f;
		subject = 0;
		overheadShown = false;
		cutKnown = false;
		active = true;
        treeOnly = a_treeOnly;
		closing = false;
		driving = true;
	}

	void End()
	{
        treeLift=0.f;treePoseSaved=false;
		if (!active || closing) {
			return;
		}
		closing = true;
		subject = 0;
		Move(kCloseSeconds, 0.0f);
	}

	void Reset()
	{
        treeLift=0.f;treeOnly=false;treePoseSaved=false;
		active = false;
		closing = false;
		subject = 0;
		forcedThird = false;
		progress = 1.0f;
		overheadShown = false;
		RestoreNear();
		cutKnown = false;
		driving = false;
		ReleaseSmoothCam();
	}

    void TreeLift(float a_amount) noexcept {
        const float next=std::clamp(a_amount,0.f,1.f);
        const float previous=treeLift.exchange(next,std::memory_order_relaxed);
        if((previous<=0.f && next>0.f) || (previous>0.f && next<=0.f)) {
            if(auto* tasks=SKSE::GetTaskInterface())tasks->AddTask([] {
                if(treeLift.load(std::memory_order_relaxed)>0.f) {if(!active)Begin(true);}
                else if(treeOnly)End();
            });
        }
    }

	void Focus(RE::FormID a_actor, bool a_portrait) noexcept
	{
		portraitWanted.store(a_portrait, std::memory_order_relaxed);
		focus.store(a_actor, std::memory_order_relaxed);
	}

	void Turn(float a_yaw, float a_pitch) noexcept
	{
		yawIn.fetch_add(a_yaw, std::memory_order_relaxed);
		pitchIn.fetch_add(a_pitch, std::memory_order_relaxed);
	}

	void Zoom(float a_steps) noexcept
	{
		zoomIn.fetch_add(a_steps, std::memory_order_relaxed);
	}

	void Pan(float a_right, float a_forward) noexcept
	{
		panRightIn.fetch_add(a_right, std::memory_order_relaxed);
		panForwardIn.fetch_add(a_forward, std::memory_order_relaxed);
	}

	void Recentre() noexcept
	{
		recentreIn.store(true, std::memory_order_relaxed);
	}

	void SetOverhead(bool a_on) noexcept
	{
		overheadWanted.store(a_on, std::memory_order_relaxed);
	}

	bool Overhead() noexcept
	{
		return overheadWanted.load(std::memory_order_relaxed) && driving.load(std::memory_order_relaxed);
	}

	bool Driving() noexcept
	{
		return driving.load(std::memory_order_relaxed);
	}
}

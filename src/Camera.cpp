#include "FWM/Camera.h"

#include "FWM/Log.h"
#include "FWM/Settings.h"

#define SMOOTHCAM_API_COMMONLIB
#include "SmoothCamAPI.h"

namespace FWM::Camera
{
	namespace
	{
		std::atomic<RE::FormID> focus{ 0 };
		std::atomic<bool>       portraitWanted{ false };  // no fight on: face to face

		// Seconds for each kind of move. Every move is a timed ease-in-out from wherever
		// the camera is shown to where it should be, so nothing ever jumps or lurches.
		constexpr float kOpenSeconds = 0.95f;
		constexpr float kSwitchSeconds = 0.85f;
		constexpr float kCloseSeconds = 0.7f;
		constexpr float kArc = 45.0f;  // a switch rises a little through its middle

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

		// Pulls the camera in front of whatever stands between it and the subject.
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
		// first time a companion is framed and holds it.
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
			}
			const auto&        settings = Settings::Get();
			const RE::NiPoint3 anchor{ ground.x, ground.y, ground.z + eyeHeight };
			if (a_portrait) {
				// Face to face, close, at eye height: in front of where they are turned.
				const float        heading = a_actor->GetAngleZ();
				const RE::NiPoint3 facing{ std::sin(heading), std::cos(heading), 0.0f };
				const RE::NiPoint3 face{ anchor.x, anchor.y, anchor.z - 4.0f };
				RE::NiPoint3       place = face + facing * settings.portraitDistance;
				place.z += 3.0f;
				return Pose{ Clear(face, place, a_actor), face };
			}
			const RE::NiPoint3 right{ bearing.y, -bearing.x, 0.0f };
			RE::NiPoint3       place = anchor - bearing * settings.cameraDistance + right * (side * 45.0f);
			place.z += settings.cameraHeight;
			return Pose{ Clear(anchor, place, a_actor), { anchor.x, anchor.y, anchor.z - 18.0f } };
		}

		void Finish(RE::PlayerCamera* a_camera)
		{
			active = false;
			closing = false;
			subject = 0;
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

			// Where the camera belongs now: the chosen companion, or home.
			Pose target = Home();
			if (!closing) {
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
				// Settled: keep gently with a companion who shifts while time is slow.
				shown.eye = Follow(shown.eye, target.eye, dt, 3.0f);
				shown.look = Follow(shown.look, target.look, dt, 4.0f);
			}
			Write(root, shown, dt);

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

	void Begin()
	{
		if (!Settings::Get().cameraGlide) {
			return;
		}
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* root = camera ? camera->cameraRoot.get() : nullptr;
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!root || !player) {
			return;
		}
		if (active && closing) {
			// Reopened while easing home: turn around from wherever the camera is.
			closing = false;
			subject = 0;
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
		active = true;
		closing = false;
	}

	void End()
	{
		if (!active || closing) {
			return;
		}
		closing = true;
		subject = 0;
		Move(kCloseSeconds, 0.0f);
	}

	void Reset()
	{
		active = false;
		closing = false;
		subject = 0;
		forcedThird = false;
		progress = 1.0f;
		ReleaseSmoothCam();
	}

	void Focus(RE::FormID a_actor, bool a_portrait) noexcept
	{
		portraitWanted.store(a_portrait, std::memory_order_relaxed);
		focus.store(a_actor, std::memory_order_relaxed);
	}
}

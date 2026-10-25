#include "FWM/PartyBalance.h"
#include "FWM/Blasts.h"
#include "FWM/EffectAudio.h"

#include "FWM/Abilities.h"
#include "FWM/Effects.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/PathBonus.h"
#include "FWM/Posing.h"

namespace FWM::Blasts
{
	namespace
	{
		constexpr float kReachDown = 250.0f;   // how far above or below its middle an enemy is still struck
		constexpr float kBurnSeconds = 3.0f;   // fire burns on this long
		constexpr float kBurnShare = 0.3f;     // taking this share of the blow again meanwhile
		constexpr float kLightLift = 60.0f;    // the flash of light hangs this far over the ground
		constexpr float kOrbReach = 220.0f;    // and the flying light's reaches this far
		constexpr std::array<float, 3> kMarked{ kBurnSeconds, 2.0f, 1.2f };  // how long each element shows on a body struck
		constexpr std::array<float, 3> kAfter{ 0.15f, 1.15f, 0.0f };      // when the second sound comes: the roll, the shatter (lightning's come with its restrikes)
		constexpr std::array<float, 3> kGrandAfter{ 0.4f, 1.5f, 0.0f };   // and a grand blast's again: the smaller explosions, the great crystal breaking
		constexpr std::array<std::string_view, 3> kNames{ "fire", "frost", "lightning" };

		struct Part
		{
			RE::NiPointer<RE::NiAVObject>      node;
			std::vector<RE::BSShaderProperty*> shaders;
			bool                             addsLight{ true };
			RE::NiPointer<RE::NiAVObject>      aura;
			std::vector<RE::BSShaderProperty*> auraShaders;
		};

		struct Live
		{
			std::string                             name;  // the mage's
			Strike                                  strike;
			RE::ActorHandle                         mage;
			RE::ActorHandle                         target;
			RE::NiPoint3                            from;  // the hand it left
			RE::NiPoint3                            to;    // the feet it flies at, followed until it lands
			float                                   flight{ 0.0f };
			float                                   age{ 0.0f };
			bool                                    struck{ false };
			std::size_t                             afters{ 0 };  // second sounds played
			bool                                    grandAfter{ false };
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			std::size_t                             copy{ 0 };  // which of the mesh's two names it plays

			// Its parts, found once the mesh has loaded.
			bool                               found{ false };
			Part                               orb;
			RE::NiPointer<RE::NiAVObject>      body;   // "Blast"
			std::vector<Part>                  parts;  // in Blast::Parts() order
			std::vector<RE::BSShaderProperty*> flowing;
			Posing::Lamp                       lamp;

			int   hit{ 0 };
			float dealt{ 0.0f };
			int   looked{ 0 };  // Inspect's moments seen
		};
		std::vector<Live> blasts;

		struct Burn
		{
			RE::ActorHandle enemy;
			float           perSecond{ 0.0f };
			float           left{ 0.0f };
            RE::ActorHandle by;
		};
		std::vector<Burn> burns;

		std::size_t Index(Blast::Element a_element) { return static_cast<std::size_t>(a_element); }
		Blast::Vec  Pure(const RE::NiPoint3& a_p) { return { a_p.x, a_p.y, a_p.z }; }

		RE::NiMatrix3 Ni(const Blast::Mat& a_m)
		{
			RE::NiMatrix3 out;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					out.entry[i][j] = a_m[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
				}
			}
			return out;
		}

		RE::NiPoint3 HandOf(RE::Actor* a_mage)
		{
			auto* model = a_mage->Get3D();
			if (auto* hand = model ? model->GetObjectByName("NPC R Hand [RHnd]") : nullptr) {
				return hand->world.translate;
			}
			return a_mage->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, 110.0f };
		}

		// Its own +Z leant by a_tilt toward a_yaw, as Blast describes a part.
		RE::NiMatrix3 Turn(float a_yaw, float a_tilt)
		{
			const float   cy = std::cos(a_yaw), sy = std::sin(a_yaw), ct = std::cos(a_tilt), st = std::sin(a_tilt);
			RE::NiMatrix3 m;
			m.entry[0][0] = cy * ct;
			m.entry[0][1] = -sy;
			m.entry[0][2] = cy * st;
			m.entry[1][0] = sy * ct;
			m.entry[1][1] = cy;
			m.entry[1][2] = sy * st;
			m.entry[2][0] = -st;
			m.entry[2][1] = 0.0f;
			m.entry[2][2] = ct;
			return m;
		}

		RE::NiAVObject* RootOf(const Live& a_live) { return Posing::RootOf(a_live.effect.get()); }

		// What the game really holds of a blast: whether each shape is in the scene and where, what its glow
		// draws with and how strongly. Once per element and moment a session, while fire and lightning that
		// never show in game are looked into.
		void Inspect(const Live& a_live, std::string_view a_when)
		{
			static std::unordered_set<std::string> told;
			const auto element = kNames[Index(a_live.strike.element)];
			if (!told.insert(std::format("{} {}", element, a_when)).second) {
				return;
			}
			auto* root = RootOf(a_live);
			if (!root || !a_live.body) {
				auto* model = a_live.effect ? a_live.effect->particleObject.get() : nullptr;
				Log::Info("[Look] {} blast {}: not posed (effect {}, age {:.2f} of {:.2f}, 3D '{}' in the scene {}, Orb {}, Blast {}).", element, a_when,
					a_live.effect != nullptr, a_live.effect ? a_live.effect->age : 0.0f, a_live.effect ? a_live.effect->lifetime : 0.0f, model ? model->name.c_str() : "none",
					model && model->parent, model && model->GetObjectByName("Orb"), model && model->GetObjectByName("Blast"));
				return;
			}
			auto culled = [&](RE::NiAVObject* a_object) {
				for (auto* at = a_object; at; at = at->parent) {
					if (at->GetAppCulled()) {
						return true;
					}
					if (at == root) {
						break;
					}
				}
				return false;
			};
			const auto& rb = root->worldBound;
			const auto& b = a_live.body->world;
			Log::Info("[Look] {} blast {}: age {:.2f} of {:.2f}, root '{}' under '{}' culled {} at ({:.0f}, {:.0f}, {:.0f}) x{:.2f} bound r {:.0f} at ({:.0f}, {:.0f}, {:.0f}); "
					  "body at ({:.0f}, {:.0f}, {:.0f}) x{:.2f} culled {}; aimed at ({:.0f}, {:.0f}, {:.0f}).",
				element, a_when, a_live.effect->age, a_live.effect->lifetime, root->name.c_str(), root->parent ? root->parent->name.c_str() : "", culled(root),
				root->world.translate.x, root->world.translate.y, root->world.translate.z, root->world.scale, rb.radius, rb.center.x, rb.center.y, rb.center.z,
				b.translate.x, b.translate.y, b.translate.z, b.scale, culled(a_live.body.get()), a_live.to.x, a_live.to.y, a_live.to.z);
			std::unordered_map<RE::BSShaderMaterial*, int> uses;
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
				if (auto* shader = a_geometry->GetGeometryRuntimeData().shaderProperty.get(); shader && shader->material) {
					++uses[shader->material];
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			RE::BSVisit::TraverseScenegraphGeometries(a_live.body.get(), [&](RE::BSGeometry* a_geometry) {
				const auto& data = a_geometry->GetGeometryRuntimeData();
				auto*       glow = netimmerse_cast<RE::BSEffectShaderProperty*>(data.shaderProperty.get());
				auto*       material = glow ? static_cast<RE::BSEffectShaderMaterial*>(glow->material) : nullptr;
				if (!material) {
					Log::Info("[Look]   {}: no glow shader.", a_geometry->name.c_str());
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto*         texture = material->sourceTexture.get();
				std::uint32_t w = 0, h = 0, mips = 0, layers = 0;
				if (texture && texture->rendererTexture) {
					texture->GetTextureDimensionsAndMipInfo(&w, &h, &mips, &layers);
				}
				const auto& c = material->baseColor;
				const auto& at = a_geometry->world.translate;
				Log::Info("[Look]   {}: culled {} drawn {} at ({:.0f}, {:.0f}, {:.0f}) x{:.2f} r {:.0f} | {} {} {}x{} m{} | colour ({:.2f}, {:.2f}, {:.2f}) a {:.3f} x{:.2f} shader a {:.2f} | "
						  "flags {:016X} blend {:04X} | material {} shared by {}.",
					a_geometry->name.c_str(), culled(a_geometry), data.rendererData != nullptr, at.x, at.y, at.z, a_geometry->world.scale, a_geometry->worldBound.radius,
					material->sourceTexturePath.c_str(), texture ? (texture->rendererTexture ? "loaded" : "NOT LOADED") : "NO TEXTURE", w, h, mips, c.red, c.green, c.blue, c.alpha,
					material->baseColorScale, glow->alpha, glow->flags.underlying(), data.alphaProperty ? data.alphaProperty->alphaFlags : 0,
					static_cast<const void*>(material), uses[material]);
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		bool Find(Live& a_live)
		{
			auto* root = RootOf(a_live);
			if (!root) {
				return false;
			}
			auto collect = [&](RE::NiAVObject* a_node, std::vector<RE::BSShaderProperty*>& a_out) {
				a_out = Posing::ShadersOf(a_node);
				std::copy_if(a_out.begin(), a_out.end(), std::back_inserter(a_live.flowing), Posing::Noisy);
			};
			a_live.orb.node.reset(root->GetObjectByName("Orb"));
			a_live.body.reset(root->GetObjectByName("Blast"));
			if (!a_live.orb.node || !a_live.body) {
				return false;
			}
			collect(a_live.orb.node.get(), a_live.orb.shaders);
			int missing = 0;
			for (const auto name : Blast::Parts(a_live.strike.element)) {
				Part part;
				part.node.reset(root->GetObjectByName(std::string(name)));
				if (part.node) {
					collect(part.node.get(), part.shaders);
					part.addsLight = Posing::AddsLight(part.node.get());
					if (a_live.strike.element == Blast::Element::kFrost) {
						part.aura.reset(part.node->GetObjectByName(std::string(name) + "Aura"));
						if (part.aura) {
							// collect already made these materials unique and collected their scrolling textures.
							RE::BSVisit::TraverseScenegraphGeometries(part.aura.get(), [&](RE::BSGeometry* a_geometry) {
								if (auto* shader = a_geometry->GetGeometryRuntimeData().shaderProperty.get()) {
									part.auraShaders.push_back(shader);
								}
								return RE::BSVisit::BSVisitControl::kContinue;
							});
							std::erase_if(part.shaders, [&](RE::BSShaderProperty* a_shader) {
								return std::ranges::find(part.auraShaders, a_shader) != part.auraShaders.end();
							});
						}
					}
				} else {
					++missing;
				}
				a_live.parts.push_back(std::move(part));
			}
			a_live.found = true;
			Log::Info("[Paths] {}'s {} blast shows {:.2f} s in{}."sv, a_live.name, kNames[Index(a_live.strike.element)], a_live.age,
				missing ? std::format(", {} of its parts missing from the mesh", missing) : "");
			return true;
		}

		// Real light: one point light under the effect's root, gone before the effect is.
		void Light(Live& a_live, const RE::NiPoint3& a_at, const Blast::Color& a_color, float a_strength, float a_reach)
		{
			Posing::Shine(a_live.lamp, RootOf(a_live), a_at, a_color, a_strength * Blast::kLightFade, a_reach);
		}

		void Fly(Live& a_live)
		{
			if (auto target = a_live.target.get(); target && !target->IsDead() && target->Is3DLoaded()) {
				a_live.to = target->GetPosition();
			}
			const auto orb = Blast::OrbAt(a_live.strike.element, Pure(a_live.from), Pure(a_live.to), a_live.age / a_live.flight);
			const RE::NiPoint3 at{ orb.at.x, orb.at.y, orb.at.z };
			if (auto* root = RootOf(a_live); root && a_live.found) {
				Posing::PlaceInWorld(root, a_live.orb.node.get(), at, Turn(orb.yaw, orb.tilt), 1.0f);
				Posing::Paint(a_live.orb.shaders, orb.color, orb.alpha);
				Posing::Refresh(a_live.orb.node.get());
			}
			Light(a_live, at, orb.color, orb.light, kOrbReach);
		}

		// It lands: everyone within its reach is struck, harder the nearer its middle.
		void Land(Live& a_live)
		{
			a_live.struck = true;
			const auto& s = a_live.strike;
			const auto  element = Index(s.element);
			auto        mage = a_live.mage.get();
			constexpr std::array<RE::ActorValue, 3> resists{ RE::ActorValue::kResistFire, RE::ActorValue::kResistFrost, RE::ActorValue::kResistShock };
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (!enemy || !Game::Usable(enemy.get())) {
					continue;
				}
				const auto  at = enemy->GetPosition();
				const float away = std::hypot(at.x - a_live.to.x, at.y - a_live.to.y);
				if (away > s.radius || std::abs(at.z - a_live.to.z) > kReachDown) {
					continue;
				}
				auto*       values = enemy->AsActorValueOwner();
				const float resist = std::min(values->GetActorValue(resists[element]), 85.0f) / 100.0f;
				const float magic = std::clamp(values->GetActorValue(RE::ActorValue::kResistMagic), 0.0f, 85.0f) / 100.0f;
				const float power = s.damage * Blast::Share(away, s.radius) * (1.0f - resist) * (1.0f - magic);
				const float amount = power;
				if (power <= 0.0f) {
					continue;
				}
				const float dealt = PartyBalance::Damage(mage.get(), enemy.get(), amount);
				// Searing supports subsequent attacks; it cannot amplify its own initial hit.
				if (s.searing > 0.0f && !enemy->IsDead()) {
					for (const auto value : resists) {
						Abilities::Lift(enemy->GetFormID(), value, -s.searing, s.searingSeconds, s.source);
					}
				}
				switch (s.element) {
				case Blast::Element::kFire:
					burns.push_back({ enemy->GetHandle(), amount * kBurnShare / kBurnSeconds, kBurnSeconds, a_live.mage });
					break;
				case Blast::Element::kFrost:
					values->DamageActorValue(RE::ActorValue::kStamina, power);
					break;
				default:
					values->DamageActorValue(RE::ActorValue::kMagicka, power * 0.5f);
					break;
				}
				if (s.stagger > 0.0f && away <= s.radius * Blast::kStaggerWithin) {
					Game::Stagger(enemy.get(), s.stagger, mage.get());
				}
				Effects::Struck(enemy.get(), element, kMarked[element]);
				++a_live.hit;
				a_live.dealt += dealt;
			}
			Posing::PlayAt(Forms::Get().blastSounds[element], a_live.to);
			if (s.grand) EffectAudio::Begin(Soundscape::Kind::Impact,mage.get(),a_live.to,2.0f,3);
			Log::Info("[Paths] {}'s {}{} blast strikes {} {} for {:.0f} in all (reach {:.0f}, {:.0f} at the middle)."sv, a_live.name, s.grand ? "grand " : "", kNames[element],
				a_live.hit, a_live.hit == 1 ? "enemy" : "enemies", a_live.dealt, s.radius, s.damage);
		}

		void Show(Live& a_live)
		{
			const float age = a_live.age - a_live.flight;
			const auto  element = Index(a_live.strike.element);
			// Lightning cracks again with every strike after the first.
			const auto afters = a_live.strike.element == Blast::Element::kShock ? std::span<const float>(Blast::kRestrikes) : std::span<const float>(&kAfter[element], 1);
			while (a_live.afters < afters.size() && age >= Blast::Real(afters[a_live.afters])) {
				++a_live.afters;
				Posing::PlayAt(Forms::Get().blastAfter[element], a_live.to);
			}
			if (a_live.strike.grand && kGrandAfter[element] > 0.0f && !a_live.grandAfter && age >= Blast::Real(kGrandAfter[element])) {
				a_live.grandAfter = true;
				Posing::PlayAt(Forms::Get().blastAfter[element], a_live.to);
			}
			const auto look = Blast::At(a_live.strike.element, age, a_live.strike.grand);
			auto*      root = RootOf(a_live);
			if (root && a_live.found) {
				Posing::Paint(a_live.orb.shaders, look.lightColor, 0.0f);
				a_live.orb.node->local.scale = 0.001f;
				Posing::Refresh(a_live.orb.node.get());
				const float size = a_live.strike.radius / Blast::kMeshRadius;
				Posing::PlaceInWorld(root, a_live.body.get(), a_live.to, {}, size);
				// Lightning turns to the camera; the blast's own node is square to the world, so a part's turn is its world turn.
				auto*      eye = RE::Main::WorldRootCamera();
				const auto camera = eye ? eye->world.translate : a_live.to + RE::NiPoint3{ 0.0f, -500.0f, 200.0f };
				for (std::size_t i = 0; i < a_live.parts.size(); ++i) {
					const auto& p = look.parts[i];
					auto&       part = a_live.parts[i];
					if (!part.node) {
						continue;
					}
					part.node->local.translate = { p.at.x, p.at.y, p.at.z };
					part.node->local.rotate = p.faces ? Ni(Blast::Facing(p, Pure(camera - (a_live.to + RE::NiPoint3{ p.at.x, p.at.y, p.at.z } * size)))) : Turn(p.yaw, p.tilt);
					part.node->local.scale = std::max(p.scale, 0.001f);
					// Solid ice blends onto the scene: fade its opacity, so it never leaves a dark shell.
					// Additive flame, mist and glints keep fading their light as before.
					if (part.addsLight) {
						Posing::Paint(part.shaders, p.color, p.alpha);
					} else {
						Posing::PaintBlended(part.shaders, p.color, p.alpha);
					}
					if (part.aura) {
						part.aura->local.translate = { 0.0f, 0.0f, p.auraLift };
						part.aura->local.rotate = Turn(p.auraYaw, 0.0f);
						part.aura->local.scale = 1.0f;
						Posing::Paint(part.auraShaders, p.color, p.auraAlpha);
					}
				}
				// The noise drifts up through the fire and the mist.
				Posing::Drift(a_live.flowing, -age * Blast::kFlow);
				Posing::Refresh(a_live.body.get());
				constexpr std::array<std::pair<float, std::string_view>, 3> kMoments{ { { 0.0f, "as it lands" }, { 0.3f, "0.3 s in" }, { 0.8f, "0.8 s in" } } };
				if (a_live.looked < static_cast<int>(kMoments.size()) && age >= kMoments[static_cast<std::size_t>(a_live.looked)].first) {
					Inspect(a_live, kMoments[static_cast<std::size_t>(a_live.looked++)].second);
				}
			}
			Light(a_live, a_live.to + RE::NiPoint3{ 0.0f, 0.0f, kLightLift }, look.lightColor, look.light, a_live.strike.radius * Blast::kLightReach);
		}

		void Finish(Live& a_live)
		{
			Posing::Out(a_live.lamp);
			if (a_live.effect) {
				a_live.effect->lifetime = 0.0f;
			}
		}
	}

	bool Cast(RE::Actor* a_mage, std::string a_name, RE::Actor* a_target, const Strike& a_strike, const RE::NiPoint3* a_point)
	{
		if (!a_mage || (!a_target && !a_point)) {
			return false;
		}
		Live live;
		live.name = std::move(a_name);
		live.strike = a_strike;
		live.mage = a_mage->GetHandle();
		live.target = a_point ? RE::ActorHandle{} : a_target->GetHandle();
		live.from = HandOf(a_mage);
		live.to = a_point ? *a_point : a_target->GetPosition();
		live.flight = Blast::Flight(live.from.GetDistance(live.to));
		// The mesh's other name if one of this element still plays.
		live.copy = std::ranges::any_of(blasts, [&](const Live& a_other) { return a_other.strike.element == a_strike.element && a_other.copy == 0; }) ? 1 : 0;
		live.effect.reset(Effects::Blast(a_target ? a_target : a_mage, Index(a_strike.element), live.copy, live.to, live.flight + Blast::Seconds(a_strike.element, a_strike.grand) + 0.3f));
		if (!live.effect) {
			Log::Info("[Look] {} blast: the game made no effect for it (target's cell attached {}).", kNames[Index(a_strike.element)],
				a_mage->GetParentCell() && a_mage->GetParentCell()->IsAttached());
		}
		Posing::PlayAt(a_strike.release, live.from);
		blasts.push_back(std::move(live));
		return true;
	}

	void Tick(float a_dt)
	{
		for (auto& live : blasts) {
			live.age += a_dt;
			if (!live.found && !Find(live) && live.age >= 0.5f && live.looked == 0) {
				live.looked = 3;
				Inspect(live, "never found");
			}
			if (!live.struck) {
				if (live.age < live.flight) {
					Fly(live);
				} else {
					Land(live);
				}
			}
			if (live.struck) {
				Show(live);
			}
		}
		std::erase_if(blasts, [](Live& a_live) {
			const bool gone = a_live.effect && (!a_live.effect->cell || !a_live.effect->cell->IsAttached());
			if (!a_live.struck || (!gone && a_live.age - a_live.flight < Blast::Seconds(a_live.strike.element, a_live.strike.grand))) {
				return false;
			}
			Finish(a_live);
			return true;
		});
		std::erase_if(burns, [a_dt](Burn& a_burn) {
			auto enemy = a_burn.enemy.get();
			if (!enemy || enemy->IsDead()) {
				return true;
			}
			PartyBalance::Damage(a_burn.by.get().get(), enemy.get(), a_burn.perSecond * std::min(a_dt, a_burn.left));
			a_burn.left -= a_dt;
			return a_burn.left <= 0.0f;
		});
	}

	void Clear()
	{
		for (auto& live : blasts) {
			Finish(live);
		}
		blasts.clear();
		burns.clear();
	}
}

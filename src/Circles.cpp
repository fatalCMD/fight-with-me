#include "FWM/PartyBalance.h"
#include "FWM/Circles.h"
#include "FWM/EffectAudio.h"

#include "FWM/Effects.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/HealCircle.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/PathBonus.h"
#include "FWM/Paths.h"
#include "FWM/Posing.h"

namespace FWM::Circles
{
	namespace
	{
		constexpr float kReachDown = 250.0f;   // how far above or below its middle someone still stands in it
		constexpr float kGlowEvery = 2.0f;     // how often someone in it shows the heal's glow
		constexpr float kBurnEvery = 1.5f;     // and an undead in it the burn's

		struct Circle
		{
			EffectAudio::ID audio{};
			RE::FormID                              healer{ 0 };
			std::string                             name;  // the healer's
			Strength                                strength;
			RE::NiPoint3                            centre;
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			RE::ObjectRefHandle                     marker;
			float                                   age{ 0.0f };

			// Its parts, found once the mesh has loaded.
			RE::NiPointer<RE::NiAVObject>                                      body;
			RE::NiPointer<RE::NiAVObject>                                      runes;
			std::array<RE::NiPointer<RE::NiAVObject>, 2>                      auras;  // its two wavy lines, the curtain under the first
			std::array<RE::NiPointer<RE::NiAVObject>, HealCircle::kMotes>      motes;
			std::vector<RE::BSShaderProperty*>                                 glow;  // floor, ring, runes
			std::array<std::vector<RE::BSShaderProperty*>, 2>                  auraGlow;
			std::vector<RE::BSShaderProperty*>                                 shimmer;  // the curtain's, drifting up
			std::array<RE::BSShaderProperty*, HealCircle::kMotes>              moteGlow{};
			// Wide Circle III's: unseen in the mesh, shown only for a circle that wide.
			RE::NiPointer<RE::NiAVObject>                                      inner;
			std::vector<RE::BSShaderProperty*>                                 innerGlow;
			std::vector<RE::BSShaderProperty*>                                 heartGlow;
			std::array<RE::NiPointer<RE::NiAVObject>, HealCircle::kGlints>     glints;
			std::array<RE::BSShaderProperty*, HealCircle::kGlints>             glintGlow{};
			std::unordered_map<RE::BSShaderProperty*, RE::NiColor>             tints;  // each glow's own colour, as the mesh gives it

			std::unordered_map<RE::FormID, float> shine;  // per actor: until their glow shows again
			std::vector<RE::FormID>               drawn;  // companions walking into it
			float                                 healed{ 0.0f };
			float                                 burned{ 0.0f };
		};
		std::vector<Circle> circles;

		// An invisible marker at its middle, made temporary so no save keeps it: what the drawn
		// companions' combat areas are centred on.
		RE::ObjectRefHandle MarkerAt(RE::Actor* a_near, const RE::NiPoint3& a_at)
		{
			auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000003B);  // XMarker
			auto* cell = a_near->GetParentCell();
			if (!base || !cell) {
				return {};
			}
			const auto made = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(base, a_at, {}, cell, a_near->GetWorldspace(), nullptr, nullptr,
				RE::ObjectRefHandle(), false, true);
			if (auto ref = made.get()) {
				ref->SetTemporary();
			}
			return made;
		}

		void Drop(RE::ObjectRefHandle& a_marker)
		{
			if (auto ref = a_marker.get()) {
				ref->Disable();
				ref->SetDelete(true);
			}
			a_marker = {};
		}

		bool Inside(const Circle& a_circle, RE::Actor* a_actor)
		{
			const auto at = a_actor->GetPosition();
			return std::hypot(at.x - a_circle.centre.x, at.y - a_circle.centre.y) <= a_circle.strength.radius && std::abs(at.z - a_circle.centre.z) <= kReachDown;
		}

		// By its brightness, its alpha held whole, as Posing::Paint fades a glow: the game copies the alpha
		// into the shader's own a frame late and multiplies the two.
		void Fade(const Circle& a_circle, RE::BSShaderProperty* a_shader, float a_alpha)
		{
			auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(a_shader);
			const auto tint = a_circle.tints.find(a_shader);
			if (glow && glow->material && tint != a_circle.tints.end()) {
				const auto& c = tint->second;
				const float visible = a_alpha * 1.65f;
				static_cast<RE::BSEffectShaderMaterial*>(glow->material)->baseColor = { c.red * visible, c.green * visible, c.blue * visible, 1.0f };
			}
		}

		// A turn about the up axis.
		RE::NiMatrix3 Turn(float a_angle)
		{
			RE::NiMatrix3 turn;
			turn.entry[0][0] = std::cos(a_angle);
			turn.entry[0][1] = -std::sin(a_angle);
			turn.entry[1][0] = std::sin(a_angle);
			turn.entry[1][1] = std::cos(a_angle);
			return turn;
		}

		// HealCircle's look onto the mesh, through whatever transform the effect gave its root.
		void Pose(Circle& a_circle, const HealCircle::Look& a_look)
		{
			auto* root = a_circle.effect ? a_circle.effect->particleObject.get() : nullptr;
			if (!root || !root->parent) {
				return;
			}
			if (!a_circle.body) {
				a_circle.body.reset(root->GetObjectByName("Circle"));
				if (!a_circle.body) {
					return;
				}
				a_circle.runes.reset(root->GetObjectByName("Runes"));
				for (std::size_t i = 0; i < a_circle.auras.size(); ++i) {
					a_circle.auras[i].reset(root->GetObjectByName(std::format("Aura{}", i)));
				}
				for (int i = 0; i < HealCircle::kMotes; ++i) {
					a_circle.motes[static_cast<std::size_t>(i)].reset(root->GetObjectByName(std::format("Mote{:02}", i)));
				}
				a_circle.inner.reset(root->GetObjectByName("Inner"));
				for (int i = 0; i < HealCircle::kGlints; ++i) {
					a_circle.glints[static_cast<std::size_t>(i)].reset(root->GetObjectByName(std::format("Glint{:02}", i)));
				}
				RE::BSVisit::TraverseScenegraphGeometries(a_circle.body.get(), [&](RE::BSGeometry* a_geometry) {
					auto*            shader = a_geometry->GetGeometryRuntimeData().shaderProperty.get();
					const std::string_view part = a_geometry->parent ? a_geometry->parent->name.c_str() : "";
					if (!shader) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					Posing::Own(shader);
					if (auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(shader); glow && glow->material) {
						const auto& c = static_cast<RE::BSEffectShaderMaterial*>(glow->material)->baseColor;
						a_circle.tints[shader] = { c.red, c.green, c.blue };
					}
					if (part.starts_with("Mote")) {
						const int index = std::atoi(part.data() + 4);
						if (index >= 0 && index < HealCircle::kMotes) {
							a_circle.moteGlow[static_cast<std::size_t>(index)] = shader;
						}
					} else if (part.starts_with("Glint")) {
						const int index = std::atoi(part.data() + 5);
						if (index >= 0 && index < HealCircle::kGlints) {
							a_circle.glintGlow[static_cast<std::size_t>(index)] = shader;
						}
					} else if (part == "Inner") {
						a_circle.innerGlow.push_back(shader);
					} else if (part == "Heart") {
						a_circle.heartGlow.push_back(shader);
					} else if (part.starts_with("Aura")) {
						a_circle.auraGlow[part == "Aura1" ? 1 : 0].push_back(shader);
						a_circle.shimmer.push_back(shader);
					} else {
						a_circle.glow.push_back(shader);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				Log::Info("[Heal] {}'s circle shows {:.2f} s in."sv, a_circle.name, a_circle.age);
			}
			const auto& parent = root->world;
			const auto  inverse = parent.rotate.Transpose();
			auto&       local = a_circle.body->local;
			local.rotate = inverse;
			local.translate = inverse * (a_circle.centre + RE::NiPoint3{0,0,6} - parent.translate) * (1.0f / parent.scale);
			local.scale = a_circle.strength.radius / HealCircle::kMeshRadius / parent.scale;
			if (a_circle.runes) {
				a_circle.runes->local.rotate = Turn(a_look.turn);
			}
			// The aura rises out of the ground and sinks back into it, its two lines turning each their own way.
			for (std::size_t i = 0; i < a_circle.auras.size(); ++i) {
				if (auto& aura = a_circle.auras[i]) {
					aura->local.rotate = Turn(a_look.waves[i]);
					aura->local.translate = { 0.0f, 0.0f, a_look.edgeHeight[i] - HealCircle::kAuraSunk * (1.0f - a_look.aura) };
				}
			}
			for (std::size_t i = 0; i < a_circle.auraGlow.size(); ++i) {
				for (auto* shader : a_circle.auraGlow[i]) Fade(a_circle, shader, a_look.edgeAlpha[i]);
			}
			for (auto* shader : a_circle.shimmer) {
				if (auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(shader); glow && glow->material) {
					for (auto& offset : glow->material->texCoordOffset) {
						offset.y = a_circle.age * HealCircle::kAuraFlow;
					}
				}
			}
			for (std::size_t i = 0; i < a_circle.motes.size(); ++i) {
				const auto& mote = a_look.motes[i];
				if (a_circle.motes[i]) {
					a_circle.motes[i]->local.translate = { mote.x, mote.y, mote.z };
					a_circle.motes[i]->local.scale = std::max(mote.size, 0.001f);
				}
				if (a_circle.moteGlow[i]) {
					Fade(a_circle, a_circle.moteGlow[i], mote.alpha);
				}
			}
			for (auto* shader : a_circle.glow) {
				Fade(a_circle, shader, a_look.ring * (0.85f + 0.15f * a_look.pulse));
			}
			// Wide Circle III: the inner circle turning against the runes, the white heart, specks rising all over.
			if (a_circle.inner) {
				a_circle.inner->local.rotate = Turn(a_look.innerTurn);
			}
			for (auto* shader : a_circle.innerGlow) {
				Fade(a_circle, shader, a_look.inner);
			}
			for (auto* shader : a_circle.heartGlow) {
				Fade(a_circle, shader, a_look.heart);
			}
			for (std::size_t i = 0; i < a_circle.glints.size(); ++i) {
				const auto& glint = a_look.glints[i];
				if (a_circle.glints[i]) {
					a_circle.glints[i]->local.translate = { glint.x, glint.y, glint.z };
					a_circle.glints[i]->local.scale = std::max(glint.size, 0.001f);
				}
				if (a_circle.glintGlow[i]) {
					Fade(a_circle, a_circle.glintGlow[i], glint.alpha);
				}
			}
			RE::NiUpdateData update{ 0.0f, RE::NiUpdateData::Flag::kNone };
			a_circle.body->Update(update);
		}

		// Everyone of the party in it heals; the undead in it burn.
		void Mend(Circle& a_circle, float a_dt)
		{
			const auto& s = a_circle.strength;
			auto        touch = [&](RE::Actor* a_actor) {
                if (!a_actor || a_actor->IsDead() || !a_actor->Is3DLoaded() || !Inside(a_circle, a_actor)) {
                    return;
                }
                auto*       values = a_actor->AsActorValueOwner();
                const float health = Game::Fraction(a_actor, RE::ActorValue::kHealth);
                if (health < 1.0f) {
                    const float give = (Game::Maximum(a_actor, RE::ActorValue::kHealth) * s.healShare + s.healFlat) *
                                       (health < Paths::kTriageLine ? 1.0f + s.triage : 1.0f) * s.bonus * a_dt;
                    values->RestoreActorValue(RE::ActorValue::kHealth, give);
                    a_circle.healed += give;
                }
                if (s.staminaShare > 0.0f) {
                    values->RestoreActorValue(RE::ActorValue::kStamina, Game::Maximum(a_actor, RE::ActorValue::kStamina) * s.staminaShare * a_dt);
                }
                auto& shine = a_circle.shine[a_actor->GetFormID()];
                shine -= a_dt;
                if (shine <= 0.0f) {
                    shine = kGlowEvery;
                    Effects::Flash(a_actor, Effects::Look::kHealAura, 1.2f);
                }
			};
			touch(RE::PlayerCharacter::GetSingleton());
			for (auto& member : Party::Members()) {
				touch(member.handle.get().get());
			}
			const auto* undead = Forms::Get().undead;
			if (!undead) {
				return;
			}
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (!enemy || !Game::Usable(enemy.get()) || !enemy->HasKeyword(undead) || !Inside(a_circle, enemy.get())) {
					continue;
				}
				const float burn = (Game::Maximum(enemy.get(), RE::ActorValue::kHealth) * s.burnShare + s.burnFlat) * (profile.boss ? 0.5f : 1.0f) * a_dt;
				a_circle.burned += PartyBalance::Damage(RE::TESForm::LookupByID<RE::Actor>(a_circle.healer), enemy.get(), burn);
				auto& shine = a_circle.shine[enemy->GetFormID()];
				shine -= a_dt;
				if (shine <= 0.0f) {
					shine = kBurnEvery;
					Effects::Flash(enemy.get(), Effects::Look::kShaken, 0.8f);
				}
			}
		}

        bool CanSeek(RE::Actor* actor, const Circle& circle, bool returning) {
            auto marker = circle.marker.get();
            if (!actor || !marker || !Party::CanHaveRole(actor) || actor->IsDead() || !actor->Is3DLoaded() || actor->AsActorState()->IsBleedingOut()) return false;
            const bool sameSpace = actor->GetParentCell() == marker->GetParentCell() ||
                (actor->GetWorldspace() && actor->GetWorldspace() == marker->GetWorldspace());
            const float distance = actor->GetPosition().GetDistance(circle.centre);
            return HealCircle::Seek(Game::Fraction(actor, RE::ActorValue::kHealth), returning,
                distance, actor->GetPosition().z - circle.centre.z, circle.strength.seconds - circle.age, sameSpace);
        }

		// Hurt companions walk into it, and back to their place once healed or as it fades.
		void Draw(Circle& a_circle)
		{
			const bool fading = a_circle.age > a_circle.strength.seconds - 2.0f;
			std::erase_if(a_circle.drawn, [&](RE::FormID a_id) {
				auto*      member = Party::Find(a_id);
				const auto actor = member ? member->handle.get() : RE::NiPointer<RE::Actor>{};
				const bool done = fading || !CanSeek(actor.get(), a_circle, true);
				if (done && actor) {
					Log::Info("[Heal] {} leaves {}'s circle at {:.0f}% health."sv, member->name, a_circle.name, Game::Fraction(actor.get(), RE::ActorValue::kHealth) * 100.0f);
				}
				return done;
			});
			if (fading || !a_circle.marker.get()) {
				return;
			}
			for (auto& member : Party::Members()) {
				auto actor = member.CanTakeRole() ? member.handle.get() : nullptr;
				if (!actor || actor->IsDead() || !actor->Is3DLoaded() || actor->AsActorState()->IsBleedingOut() ||
					std::find(a_circle.drawn.begin(), a_circle.drawn.end(), member.id) != a_circle.drawn.end()) {
					continue;
				}
				const float health = Game::Fraction(actor.get(), RE::ActorValue::kHealth);
				const float away = actor->GetPosition().GetDistance(a_circle.centre);
				if (CanSeek(actor.get(), a_circle, false)) {
					a_circle.drawn.push_back(member.id);
					Log::Info("[Heal] {} goes to {}'s circle ({:.0f}% health, {:.0f} away)."sv, member.name, a_circle.name, health * 100.0f, away);
				}
			}
		}

		void Finish(Circle& a_circle)
		{
			EffectAudio::End(a_circle.audio);
			Drop(a_circle.marker);
			if (a_circle.effect) {
				a_circle.effect->lifetime = 0.0f;
			}
			Log::Info("[Heal] {}'s circle fades after {:.1f} s: {:.0f} health given{}."sv, a_circle.name, a_circle.age, a_circle.healed,
				a_circle.burned > 0.0f ? std::format(", {:.0f} burned from the undead", a_circle.burned) : "");
		}
	}

	bool Raise(RE::FormID a_healer, std::string a_name, RE::Actor* a_on, const Strength& a_strength)
	{
		if (!a_on || a_strength.seconds <= 0.0f) {
			return false;
		}
		Circle circle;
		circle.healer = a_healer;
		circle.name = std::move(a_name);
		circle.strength = a_strength;
		// Only the undead burn is damage. Healing, stamina and their bonuses stay intact.
		circle.centre = a_on->GetPosition();
		circle.effect.reset(Effects::Circle(a_on, circle.centre, a_strength.seconds + 0.2f));
		circle.audio=EffectAudio::Begin(Soundscape::Kind::Circle,a_on,circle.centre,a_strength.seconds,a_strength.grand?3:1);
		circle.marker = MarkerAt(a_on, circle.centre);
		Log::Info("[Heal] {} raises a circle of {:.0f} under {} for {:.0f} s ({:.1f}% health{} a second{}){}."sv, circle.name, a_strength.radius,
			a_on->IsPlayerRef() ? "you"s : Game::Name(a_on), a_strength.seconds, a_strength.healShare * 100.0f,
			a_strength.healFlat > 0.0f ? std::format(" + {:.0f}", a_strength.healFlat) : "",
			a_strength.staminaShare > 0.0f ? std::format(", {:.0f}% stamina", a_strength.staminaShare * 100.0f) : "",
			circle.effect ? (a_strength.grand ? ", grand (Wide Circle III)" : "") : "; its look could not be shown");
		circles.push_back(std::move(circle));
		return true;
	}

	bool Standing(RE::FormID a_healer)
	{
		return std::any_of(circles.begin(), circles.end(), [a_healer](const Circle& a_circle) { return a_circle.healer == a_healer; });
	}

	void Tick(float a_dt)
	{
		for (auto& circle : circles) {
			circle.age += a_dt;
			const auto look = HealCircle::At(circle.age, circle.strength.seconds, circle.strength.grand);
			Pose(circle, look);
			// The visual fade must not shorten or extend the real healing window.
			const float active = std::clamp(circle.strength.seconds - (circle.age - a_dt), 0.0f, std::max(a_dt, 0.0f));
			if (active > 0) Mend(circle, active);
			Draw(circle);
		}
		std::erase_if(circles, [](Circle& a_circle) {
			if (a_circle.age < a_circle.strength.seconds) {
				return false;
			}
			Finish(a_circle);
			return true;
		});
	}

	std::optional<Pull> PullFor(RE::Actor* a_member)
	{
		if (!a_member) {
			return std::nullopt;
		}
        std::optional<Pull> best;
        float nearest = std::numeric_limits<float>::max();
        for (const auto& circle : circles) {
            if (std::find(circle.drawn.begin(), circle.drawn.end(), a_member->GetFormID()) == circle.drawn.end() || !CanSeek(a_member, circle, true)) continue;
            if (auto marker = circle.marker.get()) {
                const float distance = a_member->GetPosition().GetDistance(circle.centre);
                if (distance < nearest) {
                    nearest = distance;
                    best = Pull{ marker.get(), circle.strength.radius * 0.5f };
                }
            }
        }
        return best;
	}

	void Clear()
	{
		for (auto& circle : circles) {
			Finish(circle);
		}
		circles.clear();
	}
}

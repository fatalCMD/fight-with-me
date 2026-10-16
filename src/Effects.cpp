#include "FWM/Effects.h"

#include "FWM/EffectAssets.h"
#include "FWM/Forms.h"
#include "FWM/Log.h"
#include "FWM/Settings.h"

#include <unordered_set>

namespace FWM::Effects
{
	namespace
	{
		struct Paint
		{
			std::uint8_t r, g, b;
			float        rest;     // the rim's strength once the first light has faded, 0..1
			float        falloff;  // higher is a thinner rim
			float        flash;    // seconds a Flash lasts; 0 for the glows that are kept
			bool         aura;     // flames and sparks rising round them, not only a rim
			const char*  palette;  // an aura's flames take their colour from this gradient
		};

		// Each starts bright and settles to a faint rim, so it is seen without hiding anyone.
		// Red is kept for enemies a taunt holds; the tank's own flash is gold.
		constexpr std::array<Paint, static_cast<std::size_t>(Look::kCount)> paints{ {
			{ 235, 45, 32, 0.55f, 2.2f, 0.0f, false },    // held
			{ 255, 190, 105, 0.45f, 2.6f, 0.0f, false },  // braced
			{ 255, 205, 120, 0.6f, 1.6f, 1.2f, false },   // taunt
			{ 255, 214, 140, 0.35f, 2.0f, 1.2f, false },  // volley
			{ 255, 120, 45, 0.5f, 2.2f, 0.0f, false },    // quarry
			{ 255, 110, 55, 0.3f, 2.0f, 1.0f, false },    // strike
			{ 215, 230, 255, 0.3f, 1.6f, 0.9f, false },   // shaken
			{ 175, 205, 255, 0.35f, 2.0f, 1.4f, false },  // guard
			{ 70, 235, 110, 0.5f, 1.2f, 3.0f, true, R"(Effects\Gradients\GradGreenEnch.dds)" },    // heal aura
			{ 70, 165, 255, 0.5f, 1.2f, 3.0f, true, R"(Effects\Gradients\GradTurnMagic.dds)" },    // rejuvenate aura
			{ 215, 225, 240, 0.5f, 1.2f, 2.4f, true, R"(Effects\Gradients\GradBlueBright.dds)" },  // ward aura
			{ 255, 200, 110, 0.4f, 1.6f, 1.6f, false },   // rally
			{ 255, 95, 30, 0.5f, 1.8f, 0.0f, false },     // berserk
			{ 225, 240, 255, 0.55f, 2.0f, 0.0f, false },  // pinned
			{ 150, 235, 255, 0.4f, 2.2f, 0.0f, false },   // focus
			{ 185, 120, 255, 0.45f, 1.8f, 0.0f, false },  // surge
			{ 255, 228, 160, 0.6f, 1.4f, 0.0f, false },  // magnus
			{ 230, 220, 190, 0.25f, 2.0f, 1.4f, false }, // war cry
			{ 210, 170, 105, 0.25f, 2.0f, 1.8f, false }, // earthshaker
			{ 230, 190, 130, 0.25f, 2.0f, 0.8f, false }, // sunder impact
			{ 255, 160, 75, 0.3f, 2.0f, 1.2f, false },   // cleave
			{ 175, 220, 255, 0.3f, 2.2f, 0.0f, false },  // riposte
			{ 95, 235, 150, 0.3f, 2.0f, 0.0f, false },   // renewal
			{ 140, 240, 150, 0.3f, 2.0f, 1.8f, false },  // second wind
			{ 255, 205, 135, 0.35f, 2.2f, 2.0f, false }, // stand fast
			{ 245, 150, 65, 0.35f, 2.0f, 1.8f, false },  // challenge
			{ 180, 225, 245, 0.25f, 2.0f, 1.4f, false }, // arrow rain
		} };

		constexpr std::size_t kLooks = static_cast<std::size_t>(Look::kCount);

		// For the log: which looks show the theme and which show our own.
		constexpr std::array<std::string_view, kLooks> names{ "held", "braced", "taunt", "volley", "quarry", "strike", "shaken", "guard", "heal aura",
			"rejuvenate aura", "ward aura", "rally", "berserk", "pinned", "focus", "surge", "magnus", "war cry", "earthshaker", "sunder impact", "cleave",
			"riposte", "renewal", "second wind", "stand fast", "challenge", "arrow rain" };
		static_assert(!names.back().empty(), "a name for every look");

		// Our own looks, always made, whatever the theme has: a rim for every look, and for the
		// healer's and mage's auras the ritual's sparks with flames as a second layer.
		std::array<RE::TESEffectShader*, kLooks> rims{};
		std::array<RE::TESEffectShader*, kLooks> auras{};
		std::array<RE::TESEffectShader*, kLooks> flames{};
		RE::BGSArtObject*                        sparkArt{ nullptr };
		RE::BGSSoundDescriptorForm*              sparkSound{ nullptr };
		RE::BGSArtObject*                        disarmArt{ nullptr };      // the Disarm shout's own mark on its target, as our copy
		RE::BGSArtObject*                        disarmVanilla{ nullptr };  // the shout's own form, if the copy could not be made

		// A look shows the theme's art while its source mod is enabled and its meshes load. Without
		// it, or once the art turns out not to show, the look falls back to our own for the session.
		std::array<bool, kLooks>        themed{};
		std::array<std::string, kLooks> fellBack{};  // why a look has our own, for the log
		std::array<int, kLooks>         groundFailures{};

		struct Visual
		{
			RE::BGSArtObject* body{ nullptr };
			const char* bone{ nullptr };
			float groundSeconds{ 0.0f };
			std::array<RE::BGSArtObject*, Assets::weaponShapes.size()> weapons{};
		};
		std::array<Visual, kLooks> visuals{};
		std::vector<RE::BGSArtObject*> ownedArt;

		// Keep strong references only while our effects live. This also covers clone tasks
		// which have been queued but have not yet attached their art to the actor.
		struct Model
		{
			RE::NiPointer<RE::ModelReferenceEffect> effect;
			RE::ActorHandle actor;
			RE::NiPointer<RE::NiAVObject> root;
			Look look;
			bool kept;
			RE::FormID weapon;
			bool left;
		};
		std::vector<Model> models;
		std::vector<RE::BGSArtObject*> untrackedArt;  // do not repeatedly spawn art the manager cannot expose
		struct GroundBurst
		{
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			std::string modelName;  // own the source path; the native particle's modelName can be null
			bool animationChecked{ false };
		};
		std::vector<GroundBurst> groundBursts;
		std::mutex groundSaveLock;
		std::unordered_set<const RE::BSTempEffectParticle*> ownedGroundEffects;

		void ForgetGroundEffect(const RE::BSTempEffectParticle* a_effect)
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.erase(a_effect);
		}

		bool Ours(const RE::BGSArtObject* a_art)
		{
			return a_art && std::find(ownedArt.begin(), ownedArt.end(), a_art) != ownedArt.end();
		}

		bool Shows(const RE::BGSArtObject* a_art)
		{
			return a_art && std::find(untrackedArt.begin(), untrackedArt.end(), a_art) == untrackedArt.end();
		}

		// The theme has art for this look that can still be shown.
		bool HasArt(std::size_t a_look)
		{
			const auto& v = visuals[a_look];
			return Shows(v.body) || std::any_of(v.weapons.begin(), v.weapons.end(), [](const auto* art) { return Shows(art); });
		}

		void Why(std::size_t a_look, std::string a_why)
		{
			if (fellBack[a_look].empty()) {
				fellBack[a_look] = std::move(a_why);
			}
		}

		void FallBack(std::size_t a_look, std::string a_why)
		{
			if (!themed[a_look]) {
				return;
			}
			themed[a_look] = false;
			fellBack[a_look] = std::move(a_why);
			Log::Warn("[Effects] The {} look falls back to Fight With Me's own effect: {}."sv, names[a_look], fellBack[a_look]);
		}

		// Art that would not show takes every look left without art back to our own.
		void Lost(const RE::BGSArtObject* a_art)
		{
			for (std::size_t i = 0; i < kLooks; ++i) {
				const auto& v = visuals[i];
				const bool  uses = v.body == a_art || std::find(v.weapons.begin(), v.weapons.end(), a_art) != v.weapons.end();
				if (uses && !HasArt(i)) {
					FallBack(i, std::format("{} could not be shown", a_art->GetModel() ? a_art->GetModel() : "its model"));
				}
			}
		}

		RE::BGSArtObject* Art(const char* a_model)
		{
			if (!a_model || !*a_model) {
				return nullptr;
			}
			// Includes loose files and loaded BSAs; missing/disabled sources never become
			// dangling art objects. The same path remains usable if a mod's FormIDs change.
			RE::BSResourceNiBinaryStream resource(std::format("meshes\\{}", a_model));
			if (!resource.good()) {
				Log::Warn("[Effects] Missing mesh {}; Fight With Me's own effect stands in."sv, a_model);
				return nullptr;
			}
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSArtObject>();
			auto* art = factory ? static_cast<RE::BGSArtObject*>(factory->Create()) : nullptr;
			if (art) {
				art->SetModel(a_model);
				art->data.artType = RE::BGSArtObject::ArtType::kMagicHitEffect;
				ownedArt.push_back(art);
			}
			return art;
		}

		void LoadArt(RE::TESDataHandler* a_handler)
		{
			std::array<bool, static_cast<std::size_t>(Assets::Source::kCount)> loaded{};
			for (std::size_t i = 0; i < loaded.size(); ++i) {
				loaded[i] = a_handler && (a_handler->LookupLoadedModByName(Assets::plugins[i]) || a_handler->LookupLoadedLightModByName(Assets::plugins[i]));
				if (loaded[i]) {
					Log::Info("[Effects] FX source loaded: {}."sv, Assets::plugins[i]);
				} else {
					Log::Warn("[Effects] FX source not enabled: {}. Its looks use Fight With Me's own effects; install and enable it for the full ability visuals."sv,
						Assets::plugins[i]);
				}
			}
			for (const auto& spec : Assets::art) {
				const auto look = static_cast<std::size_t>(spec.look);
				const auto source = static_cast<std::size_t>(spec.source);
				if (!loaded[source]) {
					Why(look, std::format("{} is not enabled", Assets::plugins[source]));
					continue;
				}
				auto& visual = visuals[look];
				visual.body = Art(spec.model);
				visual.bone = spec.bone;
				visual.groundSeconds = spec.groundSeconds;
				if (!visual.body) {
					Why(look, std::format("meshes\\{} is missing", spec.model));
				}
			}
			const auto vibrant = static_cast<std::size_t>(Assets::Source::kVibrant);
			for (const auto& spec : Assets::weapons) {
				const auto look = static_cast<std::size_t>(spec.look);
				if (!loaded[vibrant]) {
					Why(look, std::format("{} is not enabled", Assets::plugins[vibrant]));
					continue;
				}
				auto& visual = visuals[look];
				for (std::size_t i = 0; i < Assets::weaponShapes.size(); ++i) {
					visual.weapons[i] = Art(std::format("vibrant weapons\\magic\\{}{}.nif", spec.element, Assets::weaponShapes[i]).c_str());
				}
				if (!HasArt(look)) {
					Why(look, std::format("no {} weapon mesh of Vibrant Weapons loads", spec.element));
				}
			}
		}

		// Vibrant's meshes carry their own NamedNode attachment instructions. Passing a
		// hand bone would override them and rotate/offset the effect away from the blade.
		std::optional<std::size_t> WeaponShape(const RE::TESObjectWEAP* a_weapon, bool a_left)
		{
			if (!a_weapon) {
				return std::nullopt;
			}
			const std::size_t hand = a_left ? 1 : 0;
			switch (a_weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword: return hand;
			case RE::WEAPON_TYPE::kOneHandDagger: return 2 + hand;
			case RE::WEAPON_TYPE::kOneHandAxe: return 4 + hand;
			case RE::WEAPON_TYPE::kOneHandMace: return 6 + hand;
			case RE::WEAPON_TYPE::kTwoHandSword: return a_left ? std::nullopt : std::optional<std::size_t>{ 8 };
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return a_left ? std::nullopt : std::optional<std::size_t>{ a_weapon->HasKeywordString("WeapTypeWarhammer") ? 10 : 9 };
			default: return std::nullopt;
			}
		}

		struct WantedArt
		{
			RE::Actor* actor;
			RE::BGSArtObject* art;
			const char* bone;
			float seconds;
			RE::FormID weapon{ 0 };
			bool left{ false };
		};

		void WantArt(std::vector<WantedArt>& a_out, RE::Actor* a_actor, Look a_look, float a_seconds)
		{
			if (!Settings::Get().enabled || !a_actor || a_seconds <= 0.1f || !a_actor->Is3DLoaded() || a_actor->IsDead() || !themed[static_cast<std::size_t>(a_look)]) {
				return;
			}
			const auto& visual = visuals[static_cast<std::size_t>(a_look)];
			if (visual.body && visual.groundSeconds <= 0.0f) {
				a_out.push_back({ a_actor, visual.body, visual.bone, a_seconds });
			}
			if (a_actor->AsActorState()->IsWeaponDrawn()) {
				for (const bool left : { false, true }) {
					auto* item = a_actor->GetEquippedObject(left);
					auto* weapon = item ? item->As<RE::TESObjectWEAP>() : nullptr;
					if (auto shape = WeaponShape(weapon, left); shape && visual.weapons[*shape]) {
						a_out.push_back({ a_actor, visual.weapons[*shape], nullptr, a_seconds, weapon->GetFormID(), left });
					}
				}
			}
		}

		bool Valid(const Model& a_model)
		{
			auto actor = a_model.actor.get();
			if (!actor || actor->IsDead() || !actor->Is3DLoaded() || actor->Get3D() != a_model.root.get()) {
				return false;
			}
			if (a_model.weapon) {
				auto* item = actor->GetEquippedObject(a_model.left);
				return actor->AsActorState()->IsWeaponDrawn() && item && item->GetFormID() == a_model.weapon;
			}
			return true;
		}

		bool Matches(const Model& a_model, const WantedArt& a_wanted)
		{
			return a_model.actor == a_wanted.actor->GetHandle() && a_model.effect->artObject == a_wanted.art &&
			       a_model.weapon == a_wanted.weapon && a_model.left == a_wanted.left && Valid(a_model);
		}

		void PlayArt(const WantedArt& a_wanted, Look a_look, bool a_kept)
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists || std::find(untrackedArt.begin(), untrackedArt.end(), a_wanted.art) != untrackedArt.end()) {
				return;
			}
			const auto target = a_wanted.actor->GetHandle();
			std::vector<RE::NiPointer<RE::ModelReferenceEffect>> before;
			lists->ForEachModelEffect([&](RE::ModelReferenceEffect* effect) {
				if (effect->target == target && effect->artObject == a_wanted.art) {
					before.emplace_back(effect);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			auto* root = a_wanted.actor->Get3D();
			auto* bone = a_wanted.bone && root ? root->GetObjectByName(a_wanted.bone) : nullptr;
			// On SE 1.5.97 this call returned 1, despite CommonLib's pointer declaration.
			// Never retain or dereference its result. The manager owns the actual effect;
			// obtain a strong reference there, under its lock, after creation. Spawn outside
			// ForEachModelEffect: the native call also takes the magic-effects lock.
			a_wanted.actor->ApplyArtObject(a_wanted.art, a_wanted.seconds, nullptr, false, false, bone);
			RE::NiPointer<RE::ModelReferenceEffect> created;
			lists->ForEachModelEffect([&](RE::ModelReferenceEffect* effect) {
				if (!effect->finished && effect->target == target && effect->artObject == a_wanted.art &&
					std::none_of(before.begin(), before.end(), [effect](const auto& previous) { return previous.get() == effect; })) {
					created.reset(effect);
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (created) {
				models.push_back({ std::move(created), target,
					RE::NiPointer<RE::NiAVObject>(root), a_look, a_kept, a_wanted.weapon, a_wanted.left });
			} else {
				// A failed/unobservable registration must not cause a sustained aura to be
				// spawned every tick. Leave the status shader and any finite native effect.
				untrackedArt.push_back(a_wanted.art);
				Log::Warn("[Effects] No new managed effect found for {}; further instances disabled this session."sv, a_wanted.art->GetModel());
				Lost(a_wanted.art);
			}
		}

		// Our own one-shots on someone: through the manager like the theme's art, or, once the
		// manager would not show us one, applied plainly as before the theme, so they never go missing.
		void OneShot(RE::Actor* a_actor, RE::BGSArtObject* a_art, const char* a_bone, float a_seconds)
		{
			if (Shows(a_art)) {
				PlayArt({ a_actor, a_art, a_bone, a_seconds }, Look::kShaken, false);
				return;
			}
			auto* root = a_actor->Get3D();
			a_actor->ApplyArtObject(a_art, a_seconds, nullptr, false, false, a_bone && root ? root->GetObjectByName(a_bone) : nullptr);
		}

		void KeepArt(Look a_look, std::span<const std::pair<RE::Actor*, float>> a_on)
		{
			std::vector<WantedArt> wanted;
			if (Settings::Get().abilityEffects) {
				for (const auto& [actor, seconds] : a_on) {
					WantArt(wanted, actor, a_look, seconds);
				}
			}
			std::vector<bool> found(wanted.size(), false);
			for (auto& model : models) {
				if (model.look != a_look || !model.kept || model.effect->finished) {
					continue;
				}
				const auto it = std::find_if(wanted.begin(), wanted.end(), [&](const auto& request) { return Matches(model, request); });
				const auto i = static_cast<std::size_t>(it - wanted.begin());
				if (it == wanted.end() || found[i]) {
					model.effect->finished = true;
				} else {
					found[i] = true;
					model.effect->lifetime = model.effect->age + it->seconds;
				}
			}
			for (std::size_t i = 0; i < wanted.size(); ++i) {
				if (!found[i]) {
					PlayArt(wanted[i], a_look, true);
				}
			}
		}

		void StartGroundAnimation(GroundBurst& a_burst)
		{
			auto* effect = a_burst.effect.get();
			auto* root = effect->particleObject.get();
			if (a_burst.animationChecked || !root) {
				return;  // the native clone task may not have finished yet
			}
			a_burst.animationChecked = true;
			auto* manager = root->GetController<RE::NiControllerManager>();
			auto* sequence = manager ? manager->GetSequenceByName("SpecialIdle_AreaEffect") : nullptr;
			if (!sequence) {
				Log::Warn("[Effects] Ground burst loaded without SpecialIdle_AreaEffect: {}."sv, a_burst.modelName);
				return;
			}
			// Leave a sequence already started by the particle loader running. If it is
			// inactive, explicitly start the authored area effect, once, after 3D exists.
			const bool started = !sequence->Inactive() || sequence->Activate(0, true, 1.0f, 0.0f, nullptr, false);
			if (started) {
				const float duration = (sequence->endKeyTime - sequence->beginKeyTime) / std::max(sequence->frequency, 0.01f);
				effect->lifetime = std::max(effect->lifetime, effect->age + duration + 0.2f);
				Log::Info("[Effects] Ground burst animation ready: {} ({:.1f} s)."sv, a_burst.modelName, duration);
			} else {
				Log::Warn("[Effects] Ground burst animation could not start: {}."sv, a_burst.modelName);
			}
		}

		// False only when the burst should have been made and was not.
		bool PlayGroundBurst(RE::Actor* a_actor, const Visual& a_visual)
		{
			auto* cell = a_actor->GetParentCell();
			if (!cell || !cell->IsAttached()) {
				return true;
			}
			const auto* model = a_visual.body->GetModel();
			if (!model || !*model) {
				Log::Warn("[Effects] Ground burst has no source model; skipping."sv);
				return false;
			}
			std::string modelName{ model };
			auto position = a_actor->GetPosition();
			position.z += 2.0f;  // keep the dust emitter just above the floor
			const RE::NiMatrix3 rotation;  // upright world-space burst
			// Unlike ApplyArtObject, this native function returns the particle it creates
			// (verified at SE 1.5.97 RVA 0x44BDD5). No explosion form or damage is spawned.
			auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_visual.groundSeconds,
				model, rotation, position, 1.0f, 0, nullptr);
			if (!effect) {
				Log::Warn("[Effects] Ground burst creation failed: {}."sv, modelName);
				return false;
			}
			{
				const std::scoped_lock guard(groundSaveLock);
				ownedGroundEffects.insert(effect);
			}
			groundBursts.push_back({ RE::NiPointer<RE::BSTempEffectParticle>(effect), std::move(modelName) });
			StartGroundAnimation(groundBursts.back());
			return true;
		}

		void FlashArt(RE::Actor* a_actor, Look a_look, float a_seconds)
		{
			const auto  look = static_cast<std::size_t>(a_look);
			const auto& visual = visuals[look];
			if (themed[look] && visual.body && visual.groundSeconds > 0.0f) {
				if (!PlayGroundBurst(a_actor, visual) && ++groundFailures[look] >= 3) {
					FallBack(look, "its ground burst failed three times");
				}
				return;
			}
			std::vector<WantedArt> wanted;
			WantArt(wanted, a_actor, a_look, a_seconds);
			for (const auto& request : wanted) {
				const auto it = std::find_if(models.begin(), models.end(), [&](const Model& model) {
					return !model.kept && model.look == a_look && !model.effect->finished && Matches(model, request);
				});
				if (it != models.end() && (a_look == Look::kEarthshaker || a_look == Look::kSunderImpact)) {
					// An impact must play again, especially the aftershock while the first
					// rock burst is fading. Extending its lifetime does not restart particles.
					it->effect->finished = true;
				}
				if (it != models.end() && !it->effect->finished) {
					// Two healers/pulses reaching someone together share one visible effect.
					it->effect->lifetime = std::max(it->effect->lifetime, it->effect->age + a_seconds);
				} else {
					PlayArt(request, a_look, false);
				}
			}
		}

		// With the theme's art a look keeps only our rim under it; without, an aura is our own sparks
		// and flames, and everything else the rim alone.
		RE::TESEffectShader* ShaderFor(Look a_look)
		{
			const auto i = static_cast<std::size_t>(a_look);
			return !themed[i] && auras[i] ? auras[i] : rims[i];
		}

		RE::TESEffectShader* FlamesFor(Look a_look)
		{
			const auto i = static_cast<std::size_t>(a_look);
			return themed[i] ? nullptr : flames[i];
		}

		// The shader the look no longer uses, when it has fallen back mid-session.
		RE::TESEffectShader* UnusedFor(Look a_look)
		{
			const auto i = static_cast<std::size_t>(a_look);
			return auras[i] && ShaderFor(a_look) == auras[i] ? rims[i] : auras[i];
		}

		bool Ours(const RE::TESEffectShader* a_shader)
		{
			auto in = [a_shader](const auto& a_list) { return std::find(a_list.begin(), a_list.end(), a_shader) != a_list.end(); };
			return a_shader && (in(rims) || in(auras) || in(flames) || a_shader == Forms::Get().markGlow);
		}

		// Once, after everything is made: which looks have the theme, and which have our own and why.
		void Report()
		{
			std::string theme, own;
			int         themes = 0, owns = 0;
			for (std::size_t i = 0; i < kLooks; ++i) {
				auto& list = themed[i] ? theme : own;
				if (!list.empty()) {
					list += ", ";
				}
				list += names[i];
				if (themed[i]) {
					++themes;
					continue;
				}
				++owns;
				list += std::format(" ({})", fellBack[i].empty() ? "the theme has none"s : fellBack[i]);
			}
			if (themes > 0) {
				Log::Info("[Effects] {} look(s) use the theme's art: {}."sv, themes, theme);
			}
			if (owns > 0) {
				Log::Info("[Effects] {} look(s) use Fight With Me's own effects: {}."sv, owns, own);
			}
		}

		RE::TESEffectShader* Copy(const RE::TESEffectShader* a_base)
		{
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESEffectShader>();
			auto* shader = factory ? static_cast<RE::TESEffectShader*>(factory->Create()) : nullptr;
			if (!shader) {
				return nullptr;
			}
			shader->data = a_base->data;
			shader->fillTexture.textureName = a_base->fillTexture.textureName;
			shader->particleShaderTexture.textureName = a_base->particleShaderTexture.textureName;
			shader->holesTexture.textureName = a_base->holesTexture.textureName;
			shader->membranePaletteTexture.textureName = a_base->membranePaletteTexture.textureName;
			shader->particlePaletteTexture.textureName = a_base->particlePaletteTexture.textureName;
			return shader;
		}

		RE::Color Tone(const Paint& a_paint, float a_scale, float a_white = 0.0f)
		{
			auto channel = [&](std::uint8_t a_value) {
				const float lit = static_cast<float>(a_value) * a_scale;
				return static_cast<std::uint8_t>(std::clamp(lit + (255.0f - lit) * a_white, 0.0f, 255.0f));
			};
			return RE::Color(channel(a_paint.r), channel(a_paint.g), channel(a_paint.b), 255);
		}

		// A copy of a rim shader in a new colour: only the edge, no particles, drawn where the
		// body is seen (the base draws through walls).
		RE::TESEffectShader* Rim(const RE::TESEffectShader* a_base, const Paint& a_paint)
		{
			auto* shader = Copy(a_base);
			if (!shader) {
				return nullptr;
			}
			auto& d = shader->data;
			d.membraneShaderZTestFunction = RE::D3DCMPFUNC::kEqual;
			d.flags.set(RE::EffectShaderData::Flags::kDisableParticleShader);
			d.particleShaderPersistantParticleCount = 0.0f;
			d.particleShaderFullParticleBirthRatio = 0.0f;
			d.fillTextureEffectColorKey1 = RE::Color(0, 0, 0, 0);  // added light: black adds none
			d.edgeEffectColor = Tone(a_paint, 1.0f);
			d.edgeEffectFallOff = a_paint.falloff;
			d.edgeEffectAlphaFadeInTime = 0.12f;
			d.edgeEffectFullAlphaTime = 0.35f;
			d.edgeEffectFullAlphaRatio = 1.0f;
			d.edgeEffectPersistentAlphaRatio = a_paint.rest;
			d.edgeEffectAlphaPulseAmplitude = 0.12f;
			d.edgeEffectAlphaPulseFrequency = 0.6f;
			d.edgeEffectAlphaFadeOutTime = 0.5f;
			return shader;
		}

		// The healing ritual's shader in a new colour: a wash over the body as it starts, a bright
		// rim, and a column of sparks rising round them, more and bigger than the ritual's own. Its
		// sparks take their colour from the three colour keys, with no palette in the way.
		RE::TESEffectShader* Aura(const RE::TESEffectShader* a_base, const Paint& a_paint)
		{
			auto* shader = Copy(a_base);
			if (!shader) {
				return nullptr;
			}
			auto& d = shader->data;
			d.membraneShaderZTestFunction = RE::D3DCMPFUNC::kEqual;
			d.fillTextureEffectColorKey1 = Tone(a_paint, 0.4f);
			d.fillTextureEffectAlphaFadeInTime = 0.1f;
			d.fillTextureEffectFullAlphaTime = 0.5f;
			d.fillTextureEffectFullAlphaRatio = 0.7f;
			d.fillTextureEffectPersistentAlphaRatio = 0.0f;
			d.fillTextureEffectAlphaFadeOutTime = 0.6f;
			d.edgeEffectColor = Tone(a_paint, 1.0f, 0.15f);
			d.edgeEffectFallOff = a_paint.falloff;
			d.edgeEffectAlphaFadeInTime = 0.1f;
			d.edgeEffectFullAlphaTime = 0.6f;
			d.edgeEffectFullAlphaRatio = 1.0f;
			d.edgeEffectPersistentAlphaRatio = a_paint.rest;
			d.edgeEffectAlphaFadeOutTime = 0.8f;
			d.colorKey1 = Tone(a_paint, 0.45f);
			d.colorKey2 = Tone(a_paint, 1.0f, 0.35f);
			d.colorKey3 = Tone(a_paint, 0.4f);
			d.particleShaderFullParticleBirthRatio *= 2.5f;
			d.particleShaderPersistantParticleCount *= 2.5f;
			d.particleShaderScaleKey1 = std::max(d.particleShaderScaleKey1, 6.0f);
			return shader;
		}


		// Turn Undead's flames in another colour: big flame sprites licking up the body, their
		// colour read from a gradient (green, blue or silver), with a coloured vapour over the
		// skin and a bright rim. Far bolder than the ritual's sparks, which sit on top of it.
		RE::TESEffectShader* Flames(const RE::TESEffectShader* a_base, const Paint& a_paint)
		{
			auto* shader = Copy(a_base);
			if (!shader) {
				return nullptr;
			}
			shader->particlePaletteTexture.textureName = a_paint.palette;
			auto& d = shader->data;
			d.membraneShaderZTestFunction = RE::D3DCMPFUNC::kEqual;
			d.flags.reset(RE::EffectShaderData::Flags::kDisableTextureShader);  // the vapour and rim on, too
			d.fillTextureEffectColorKey1 = Tone(a_paint, 0.35f);
			d.fillTextureEffectAlphaFadeInTime = 0.15f;
			d.fillTextureEffectFullAlphaTime = 0.9f;
			d.fillTextureEffectFullAlphaRatio = 0.7f;
			d.fillTextureEffectPersistentAlphaRatio = 0.3f;
			d.fillTextureEffectAlphaFadeOutTime = 0.8f;
			d.edgeEffectColor = Tone(a_paint, 1.0f, 0.2f);
			d.edgeEffectFallOff = 1.0f;
			d.edgeEffectAlphaFadeInTime = 0.15f;
			d.edgeEffectFullAlphaTime = 0.9f;
			d.edgeEffectFullAlphaRatio = 1.0f;
			d.edgeEffectPersistentAlphaRatio = 0.5f;
			d.edgeEffectAlphaFadeOutTime = 0.8f;
			d.particleShaderFullParticleBirthTime = std::max(d.particleShaderFullParticleBirthTime, 1.2f);
			d.particleShaderFullParticleBirthRatio *= 2.0f;
			d.particleShaderPersistantParticleCount *= 2.0f;
			return shader;
		}

		// The spark of steel on steel, from the impact of an axe on metal armor, as an art object
		// the hand can carry. Made at runtime, so kept out of saves like the shaders.
		RE::BGSArtObject* SparkArt(const RE::BGSImpactData* a_impact)
		{
			return Art(a_impact ? a_impact->GetModel() : nullptr);
		}

		// Runtime art forms have no persistent IDs; exclude only our model instances.
		struct ModelSaves
		{
			static bool Thunk(const RE::ModelReferenceEffect* a_effect)
			{
				return a_effect && Ours(a_effect->artObject) ? false : original(a_effect);
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};
		struct ParticleSaves
		{
			static bool Thunk(const RE::BSTempEffectParticle* a_effect)
			{
				bool ours;
				{
					const std::scoped_lock guard(groundSaveLock);
					ours = ownedGroundEffects.contains(a_effect);
				}
				return ours ? false : original(a_effect);
			}
			static inline REL::Relocation<decltype(Thunk)> original;
		};
		// Whether the effect manager writes an effect into a save. A copy made at runtime has no
		// form a save could name, so none of ours goes in: a save never holds one, and a load
		// starts without them.
		struct ManagerSaves
		{
			static bool Thunk(const RE::ShaderReferenceEffect* a_effect)
			{
				return a_effect && Ours(a_effect->effectData) ? false : original(a_effect);
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};
	}

	void Install()
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		LoadArt(handler);
		auto* rim = handler ? handler->LookupForm<RE::TESEffectShader>(0x0FE68D, "Skyrim.esm"sv) : nullptr;    // GhostWolfSpiritFXShader
		auto* ritual = handler ? handler->LookupForm<RE::TESEffectShader>(0x10CDC9, "Skyrim.esm"sv) : nullptr;  // HealCircleFXS
		auto* flame = handler ? handler->LookupForm<RE::TESEffectShader>(0x0E7557, "Skyrim.esm"sv) : nullptr;   // TurnUndeadFlamesFXS
		int   made = 0;
		for (std::size_t i = 0; i < paints.size(); ++i) {
			const auto& paint = paints[i];
			rims[i] = rim ? Rim(rim, paint) : nullptr;
			if (paint.aura) {
				auras[i] = ritual ? Aura(ritual, paint) : nullptr;
				flames[i] = flame ? Flames(flame, paint) : nullptr;
			}
			themed[i] = HasArt(i);
			made += ShaderFor(static_cast<Look>(i)) ? 1 : 0;
		}
		if (made < static_cast<int>(paints.size())) {
			Log::Warn("[Effects] {} of {} ability effects could not be made (ghost wolf rim {}, healing ritual {})."sv, static_cast<int>(paints.size()) - made, paints.size(),
				rim ? "found"sv : "missing"sv, ritual ? "found"sv : "missing"sv);
		}
		auto* impact = handler ? handler->LookupForm<RE::BGSImpactData>(0x04BB55, "Skyrim.esm"sv) : nullptr;  // WPNAxeVsMetalImpact
		sparkArt = SparkArt(impact);
		sparkSound = impact ? impact->sound1 : nullptr;
		disarmVanilla = handler ? handler->LookupForm<RE::BGSArtObject>(0x0829EF, "Skyrim.esm"sv) : nullptr;  // DisArmShoutTargetFX
		disarmArt = Art(disarmVanilla ? disarmVanilla->GetModel() : nullptr);
		Report();

		REL::Relocation<std::uintptr_t> shaderEffects{ RE::VTABLE_ShaderReferenceEffect[0] };
		ManagerSaves::original = shaderEffects.write_vfunc(0x2A, ManagerSaves::Thunk);
		REL::Relocation<std::uintptr_t> modelEffects{ RE::VTABLE_ModelReferenceEffect[0] };
		ModelSaves::original = modelEffects.write_vfunc(0x2A, ModelSaves::Thunk);
		REL::Relocation<std::uintptr_t> particleEffects{ RE::VTABLE_BSTempEffectParticle[0] };
		ParticleSaves::original = particleEffects.write_vfunc(0x2A, ParticleSaves::Thunk);
		Log::Info("[Effects] {} status shaders and {} owned art models ready; source spells are never cast and owned effects are kept out of saves."sv,
			made, ownedArt.size());
	}

	void Flash(RE::Actor* a_actor, Look a_look, float a_seconds)
	{
		if (!a_actor || !Settings::Get().enabled || !Settings::Get().abilityEffects || !a_actor->Is3DLoaded() || a_actor->IsDead()) {
			return;
		}
		const float seconds = a_seconds > 0.0f ? a_seconds : std::max(paints[static_cast<std::size_t>(a_look)].flash, 0.6f);
		FlashArt(a_actor, a_look, seconds);
		// After the art, so a look whose art has just failed gets our own at once.
		if (auto* shader = ShaderFor(a_look)) {
			a_actor->ApplyEffectShader(shader, seconds);
		}
		if (auto* flame = FlamesFor(a_look)) {
			a_actor->ApplyEffectShader(flame, seconds);
		}
	}

	void Sparks(RE::Actor* a_actor)
	{
		if (!a_actor || !Settings::Get().enabled || !Settings::Get().abilityEffects || !a_actor->Is3DLoaded()) {
			return;
		}
		// On the hand bone, which stays when the weapon goes.
		auto* model = a_actor->Get3D();
		auto* hand = model ? model->GetObjectByName("NPC R Hand [RHnd]") : nullptr;
		if (sparkArt) {
			OneShot(a_actor, sparkArt, "NPC R Hand [RHnd]", 1.2f);
		}
		if (disarmArt) {
			OneShot(a_actor, disarmArt, nullptr, 1.5f);
		} else if (disarmVanilla) {
			a_actor->ApplyArtObject(disarmVanilla, 1.5f);
		}
		RE::BSSoundHandle sound;
		if (auto* audio = RE::BSAudioManager::GetSingleton(); audio && sparkSound && audio->GetSoundHandle(sound, sparkSound)) {
			sound.SetPosition(a_actor->GetPosition());
			sound.SetObjectToFollow(hand ? hand : model);
			sound.Play();
		}
	}

	void Keep(Look a_look, std::span<const std::pair<RE::Actor*, float>> a_on)
	{
		const bool enabled = Settings::Get().abilityEffects && Settings::Get().enabled;
		std::vector<std::pair<RE::Actor*, float>> on;
		if (enabled) {
			for (const auto& [actor, seconds] : a_on) {
				if (!actor || seconds <= 0.1f || !actor->Is3DLoaded() || actor->IsDead()) {
					continue;
				}
				// Several arrows or archers can pin the same enemy. One visual lasts until
				// the longest remaining pin ends, rather than restarting duplicates each tick.
				const auto it = std::find_if(on.begin(), on.end(), [actor](const auto& entry) { return entry.first == actor; });
				if (it != on.end()) {
					it->second = std::max(it->second, seconds);
				} else {
					on.emplace_back(actor, seconds);
				}
			}
		}
		KeepArt(a_look, on);
		auto* shader = ShaderFor(a_look);
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!shader || !lists) {
			return;
		}
		std::vector<std::uint32_t> wanted;
		wanted.reserve(on.size());
		for (const auto& [actor, seconds] : on) {
			wanted.push_back(actor ? actor->GetHandle().native_handle() : 0);
		}
		std::vector<bool> lit(on.size(), false);
		auto*             unused = UnusedFor(a_look);
		lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
			if (a_effect && unused && a_effect->effectData == unused) {
				a_effect->finished = true;  // from before the look fell back
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (!a_effect || a_effect->effectData != shader || a_effect->finished) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			const auto it = std::find(wanted.begin(), wanted.end(), a_effect->target.native_handle());
			const auto i = static_cast<std::size_t>(it - wanted.begin());
			if (!enabled || it == wanted.end() || lit[i]) {
				a_effect->finished = true;  // no longer wanted, or a second copy
				return RE::BSContainer::ForEachResult::kContinue;
			}
			lit[i] = true;
			// A hold renewed by another taunt runs longer; one cut short ends sooner.
			const float seconds = on[i].second;
			if (std::abs(a_effect->lifetime - a_effect->age - seconds) > 0.5f) {
				a_effect->lifetime = a_effect->age + seconds;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (!enabled) {
			return;
		}
		for (std::size_t i = 0; i < on.size(); ++i) {
			auto* actor = on[i].first;
			if (!lit[i]) {
				actor->ApplyEffectShader(shader, on[i].second);
			}
		}
	}

	void Clear()
	{
		for (auto& burst : groundBursts) {
			burst.effect->lifetime = 0.0f;
			ForgetGroundEffect(burst.effect.get());
		}
		groundBursts.clear();
		for (auto& model : models) {
			model.effect->finished = true;
		}
		models.clear();
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!lists) {
			return;
		}
		// Include any native effect that could not be captured immediately after creation.
		lists->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
			if (Ours(a_effect->artObject)) {
				a_effect->finished = true;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
			if (a_effect && a_effect->effectData != Forms::Get().markGlow && Ours(a_effect->effectData)) {
				a_effect->finished = true;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	void Update()
	{
		if (!Settings::Get().abilityEffects || !Settings::Get().enabled) {
			Clear();
			return;
		}
		std::erase_if(groundBursts, [](GroundBurst& burst) {
			auto* effect = burst.effect.get();
			if (!effect->cell || !effect->cell->IsAttached() || effect->age >= effect->lifetime) {
				effect->lifetime = 0.0f;
				ForgetGroundEffect(effect);
				return true;
			}
			StartGroundAnimation(burst);
			return false;
		});
		std::erase_if(models, [](Model& model) {
			if (!Valid(model) || (model.effect->lifetime >= 0.0f && model.effect->age >= model.effect->lifetime)) {
				model.effect->finished = true;
			}
			return model.effect->finished;
		});
	}
}

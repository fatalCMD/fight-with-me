#include "FWM/Effects.h"

#include "FWM/EffectAssets.h"
#include "FWM/SupportArt.h"
#include "FWM/Posing.h"
#include "FWM/ChaosPulse.h"
#include "FWM/Forms.h"
#include "FWM/Log.h"
#include "FWM/Settings.h"
#include "FWM/WeakWill.h"

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
			{ 150, 200, 255, 0.45f, 1.4f, 0.0f, false },  // magnus
			{ 230, 220, 190, 0.25f, 2.0f, 1.4f, false }, // war cry
			{ 210, 170, 105, 0.25f, 2.0f, 1.8f, false }, // earthshaker
			{ 230, 190, 130, 0.25f, 2.0f, 0.8f, false }, // sunder impact
			{ 170, 215, 255, 0.45f, 1.6f, 1.8f, false },  // spectral wolf
			{ 175, 220, 255, 0.3f, 2.2f, 0.0f, false },  // riposte
			{ 95, 235, 150, 0.3f, 2.0f, 0.0f, false },   // renewal
			{ 140, 240, 150, 0.3f, 2.0f, 1.8f, false },  // second wind
			{ 255, 205, 135, 0.35f, 2.2f, 2.0f, false }, // stand fast
			{ 155, 110, 245, 0.35f, 2.0f, 1.8f, false }, // chaos down
			{ 180, 225, 245, 0.25f, 2.0f, 1.4f, false }, // arrow rain
			{ 255, 212, 120, 0.4f, 1.4f, 3.2f, true, R"(Effects\Gradients\GradHealMagic.dds)" },  // level up
			{ 200, 22, 18, 0.5f, 2.0f, 0.0f, false },    // bleeding
		} };

		constexpr std::size_t kLooks = static_cast<std::size_t>(Look::kCount);

		// For the log: which looks show the theme and which show our own.
		constexpr std::array<std::string_view, kLooks> names{ "held", "braced", "taunt", "volley", "quarry", "strike", "shaken", "guard", "heal aura",
			"rejuvenate aura", "ward aura", "rally", "berserk", "pinned", "focus", "surge", "magnus", "war cry", "earthshaker", "sunder impact", "spectral wolf",
			"riposte", "renewal", "second wind", "stand fast", "chaos down", "arrow rain", "level up", "bleeding" };
		static_assert(!names.back().empty(), "a name for every look");

		// Our own looks, always made, whatever the theme has: a rim for every look, and for the
		// healer's and mage's auras the ritual's sparks with flames as a second layer.
		std::array<RE::TESEffectShader*, kLooks> rims{};
		std::array<RE::TESEffectShader*, kLooks> finalAuras{};
		std::array<RE::TESEffectShader*, kLooks> auras{};
		std::array<RE::TESEffectShader*, kLooks> flames{};
		RE::BGSArtObject*                        sparkArt{ nullptr };
		RE::BGSSoundDescriptorForm*              sparkSound{ nullptr };
		RE::BGSArtObject*                        disarmArt{ nullptr };      // the Disarm shout's own mark on its target, as our copy
		RE::BGSArtObject*                        disarmVanilla{ nullptr };  // the shout's own form, if the copy could not be made
		// The furred head draws its fur with our generated texture, so it is shown only when
		// that texture is there to read; otherwise the plain head, with the vanilla wolf's fur cards.
		constexpr const char*                    kFurredHead = R"(FightWithMe\SpectralWolfHead.nif)";
		constexpr const char*                    kPlainHead = R"(FightWithMe\SpectralWolfHeadPlain.nif)";
		constexpr const char*                    kFurTexture = R"(textures\FightWithMe\SpectralFur.dds)";
		const char*                              headModel{ nullptr };      // the one installed and chosen
		// The third rank's red wolf, furred longer and thicker, chosen the same way.
		constexpr const char*                    kFurredRedHead = R"(FightWithMe\SpectralWolfHeadRed.nif)";
		constexpr const char*                    kPlainRedHead = R"(FightWithMe\SpectralWolfHeadRedPlain.nif)";
		const char*                              redHeadModel{ nullptr };
		constexpr std::array<const char*, 4> kBearFurred{ R"(FightWithMe\SpectralBearHead.nif)", R"(FightWithMe\SpectralBearHead2.nif)",
			R"(FightWithMe\SpectralBearHead3.nif)", R"(FightWithMe\SpectralBearHead4.nif)" };
		constexpr std::array<const char*, 4> kBearPlain{ R"(FightWithMe\SpectralBearHeadPlain.nif)", R"(FightWithMe\SpectralBearHeadPlain2.nif)",
			R"(FightWithMe\SpectralBearHeadPlain3.nif)", R"(FightWithMe\SpectralBearHeadPlain4.nif)" };
		constexpr std::array<const char*, 4> kBearRedFurred{ R"(FightWithMe\SpectralBearHeadRed.nif)", R"(FightWithMe\SpectralBearHeadRed2.nif)",
			R"(FightWithMe\SpectralBearHeadRed3.nif)", R"(FightWithMe\SpectralBearHeadRed4.nif)" };
		constexpr std::array<const char*, 4> kBearRedPlain{ R"(FightWithMe\SpectralBearHeadRedPlain.nif)", R"(FightWithMe\SpectralBearHeadRedPlain2.nif)",
			R"(FightWithMe\SpectralBearHeadRedPlain3.nif)", R"(FightWithMe\SpectralBearHeadRedPlain4.nif)" };
		std::array<const char*, 4> bearModels{}, bearRedModels{};
		constexpr std::array<const char*, 4> kWarSabre{ R"(FightWithMe\WarCrySabre.nif)", R"(FightWithMe\WarCrySabre2.nif)", R"(FightWithMe\WarCrySabre3.nif)", R"(FightWithMe\WarCrySabre4.nif)" };
		constexpr std::array<const char*, 4> kWarSabreRed{ R"(FightWithMe\WarCrySabreRed.nif)", R"(FightWithMe\WarCrySabreRed2.nif)", R"(FightWithMe\WarCrySabreRed3.nif)", R"(FightWithMe\WarCrySabreRed4.nif)" };
		std::array<const char*, 4> warSabreModels{}, warSabreRedModels{};
		constexpr std::array<const char*, 4> kChaosModels{ R"(FightWithMe\ChaosDown.nif)", R"(FightWithMe\ChaosDown2.nif)",
			R"(FightWithMe\ChaosDown3.nif)", R"(FightWithMe\ChaosDown4.nif)" };
		std::array<bool, 4> chaosModels{};
		constexpr std::array<const char*, 4> kChaosGrandModels{ R"(FightWithMe\ChaosDownGrand.nif)", R"(FightWithMe\ChaosDownGrand2.nif)",
			R"(FightWithMe\ChaosDownGrand3.nif)", R"(FightWithMe\ChaosDownGrand4.nif)" };
		std::array<bool, 4> chaosGrandModels{};
		constexpr std::array<const char*, WeakWill::kCopies> kWeakWillModels{ R"(FightWithMe\WeakWillHelmet.nif)", R"(FightWithMe\WeakWillHelmet2.nif)",
			R"(FightWithMe\WeakWillHelmet3.nif)", R"(FightWithMe\WeakWillHelmet4.nif)" };
		constexpr std::array<const char*, WeakWill::kCopies> kWeakWillGrandModels{ R"(FightWithMe\WeakWillHelmetGrand.nif)", R"(FightWithMe\WeakWillHelmetGrand2.nif)",
			R"(FightWithMe\WeakWillHelmetGrand3.nif)", R"(FightWithMe\WeakWillHelmetGrand4.nif)" };
		std::array<bool, WeakWill::kCopies> weakWillModels{}, weakWillGrandModels{};
		constexpr std::array<const char*, 2> kBastionModels{ R"(FightWithMe\LastBastionTower.nif)", R"(FightWithMe\LastBastionCastle.nif)" };
		std::array<bool, 2> bastionModels{};
		constexpr std::array<const char*, ChaosPulse::kBindingCopies> kChaosBindingModels{
			R"(FightWithMe\ChaosBinding.nif)",
			R"(FightWithMe\ChaosBinding2.nif)",
			R"(FightWithMe\ChaosBinding3.nif)",
			R"(FightWithMe\ChaosBinding4.nif)",
			R"(FightWithMe\ChaosBinding5.nif)",
			R"(FightWithMe\ChaosBinding6.nif)",
			R"(FightWithMe\ChaosBinding7.nif)",
			R"(FightWithMe\ChaosBinding8.nif)",
			R"(FightWithMe\ChaosBinding9.nif)",
			R"(FightWithMe\ChaosBinding10.nif)",
			R"(FightWithMe\ChaosBinding11.nif)",
			R"(FightWithMe\ChaosBinding12.nif)",
			R"(FightWithMe\ChaosBinding13.nif)",
			R"(FightWithMe\ChaosBinding14.nif)",
			R"(FightWithMe\ChaosBinding15.nif)",
			R"(FightWithMe\ChaosBinding16.nif)"
		};
		std::array<bool, ChaosPulse::kBindingCopies> chaosBindingModels{};
		constexpr const char*                    kCircleModel = R"(FightWithMe\HealingCircle.nif)";
		bool                                     circleModel{ false };
		constexpr std::array<const char*, 4> kRejuvenationModels{ R"(FightWithMe\RejuvenationCircle.nif)", R"(FightWithMe\RejuvenationCircle2.nif)",
			R"(FightWithMe\RejuvenationCircle3.nif)", R"(FightWithMe\RejuvenationCircle4.nif)" };
		std::array<bool, 4> rejuvenationModels{};
		constexpr std::array<const char*, 4> kSunderModels{ R"(FightWithMe\SunderBreak.nif)", R"(FightWithMe\SunderBreak2.nif)",
			R"(FightWithMe\SunderBreak3.nif)", R"(FightWithMe\SunderBreak4.nif)" };
		std::array<bool, 4> sunderModels{};
		std::array<std::string,16> windstepModels{};
        std::array<bool,16> windstepAvailable{};
        std::array<std::string,24> arrowArtModels{};
        std::array<bool,24> arrowArtAvailable{};
        std::array<std::string,16> disarmModels{};
		std::array<std::string,16> riposteModels{};
		std::array<bool,16> disarmAvailable{};
		std::array<bool,16> riposteAvailable{};
        std::array<std::string,24> moonModels{};
        std::array<bool,24> moonAvailable{};
        std::array<std::string,8> windModels{};
        std::array<bool,8> windAvailable{};
		constexpr std::array<const char*,4> kBerserkModels{ R"(FightWithMe\BerserkMantle.nif)", R"(FightWithMe\BerserkMantle2.nif)", R"(FightWithMe\BerserkMantle3.nif)", R"(FightWithMe\BerserkMantle4.nif)" };
		std::array<bool,4> berserkModels{};
		constexpr std::array<const char*,4> kSurgeModels{ R"(FightWithMe\ArcaneSurge.nif)", R"(FightWithMe\ArcaneSurge2.nif)", R"(FightWithMe\ArcaneSurge3.nif)", R"(FightWithMe\ArcaneSurge4.nif)" };
		std::array<bool,4> surgeModels{};
		// Each blast twice over, the same mesh by two names: the game never gave a second copy of one still
		// playing a body, so a burst right after another (the Twin perk) takes the other name.
		constexpr std::array<std::array<const char*, 2>, 3> kBlastModels{ { { R"(FightWithMe\ElementalBlastFire.nif)", R"(FightWithMe\ElementalBlastFire2.nif)" },
			{ R"(FightWithMe\ElementalBlastFrost.nif)", R"(FightWithMe\ElementalBlastFrost2.nif)" },
			{ R"(FightWithMe\ElementalBlastShock.nif)", R"(FightWithMe\ElementalBlastShock2.nif)" } } };
		std::array<std::array<bool, 2>, 3>       blastModels{};
		constexpr const char*                    kOrbModel = R"(FightWithMe\MagnusOrb.nif)";
		bool                                     orbModel{ false };
		constexpr const char*                    kQuakeModel = R"(FightWithMe\Earthshaker.nif)";
		bool                                     quakeModel{ false };
		// Twice over like the blasts, for two archers' rains at once.
		constexpr std::array<const char*, 2>     kRainModels{ R"(FightWithMe\ArrowRain.nif)", R"(FightWithMe\ArrowRain2.nif)" };
		std::array<bool, 2>                      rainModels{};
		constexpr std::array<const char*, 2>     kCloakModels{ R"(FightWithMe\WaterCloak.nif)", R"(FightWithMe\WaterCloak2.nif)" };
		std::array<bool, 2>                      cloakModels{};
		constexpr std::array<const char*, 2>     kHauntModels{ R"(FightWithMe\Haunt.nif)", R"(FightWithMe\Haunt2.nif)" };
		std::array<bool, 2>                      hauntModels{};
		std::array<RE::TESEffectShader*, 3>      elementals{};  // fire, frost and shock on a body struck by a blast

		// By Blood kind: the impact models and sounds of a werewolf's bite and of a bleeding wound.
		std::array<std::string, 3>                           bloodModels{};
		std::array<RE::BGSSoundDescriptorForm*, 3>           bloodSounds{};
		std::vector<RE::NiPointer<RE::BSTempEffectParticle>> sprays;
		std::vector<RE::NiPointer<RE::BSTempEffectParticle>> heads;  // the wolf's heads and the healer's circles: kept apart, so switching the effects off does not take them

		// Authored art uses the required Effects pack; missing art retains Skyrim-based status shaders.
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
        struct SupportMote {
            RE::NiAVObject* node{};
            RE::NiPoint3 origin{};
            std::vector<RE::BSShaderProperty*> materials;
        };
        SupportArt::Kind SupportKind(Look look)
        {
            using K=SupportArt::Kind;
            switch (look) {
            case Look::kBraced: case Look::kGuard: case Look::kWardAura: return K::Guard;
            case Look::kVolley: case Look::kFocus: return K::Focus;
            case Look::kQuarry: return K::Mark;
            case Look::kPinned: return K::Binding;
            case Look::kSurge: return K::Surge;
            case Look::kRally: return K::Rally;
            case Look::kTaunt: case Look::kWarCry: return K::Wind;
            case Look::kSunderImpact: return K::Impact;
            case Look::kLevelUp: return K::LevelUp;
            case Look::kBerserk: return K::Fire;
            case Look::kStrike: case Look::kRiposte: return K::Steel;
            case Look::kSpiritWolf: return K::Spirit;
            default: return K::Vitality;
            }
        }
		struct Model
		{
			RE::NiPointer<RE::ModelReferenceEffect> effect;
			RE::ActorHandle actor;
			RE::NiPointer<RE::NiAVObject> root;
			Look look;
			bool kept;
			RE::FormID weapon;
			bool left;
            RE::NiPointer<RE::NiAVObject> animatedRoot;
            std::array<RE::NiAVObject*, 3> parts{};
            std::array<std::vector<RE::BSShaderProperty*>, 3> materials{};
            std::array<SupportMote, 26> motes{};
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

        void LoadArt()
        {
            for (const auto& spec : Assets::art) {
                const auto look = static_cast<std::size_t>(spec.look);
                auto& visual = visuals[look];
                visual.body = Art(spec.model);
                visual.bone = spec.bone;
                visual.groundSeconds = spec.groundSeconds;
                if (!visual.body) Why(look, "required POTC Effects mesh is missing");
            }
            for (const auto& spec : Assets::weapons) {
                const auto look = static_cast<std::size_t>(spec.look);
                for (std::size_t i = 0; i < Assets::weaponShapes.size(); ++i)
                    visuals[look].weapons[i] = Art(std::format("FightWithMe\\Support\\{}{}.nif", spec.element, Assets::weaponShapes[i]).c_str());
                if (!HasArt(look)) Why(look, "required POTC Effects weapon art is missing");
            }
            Log::Info("[Effects] Standalone art: POTC Effects and Skyrim assets only."sv);
        }

        // The authored trails follow the actual weapon attachment, including left-handed weapons.
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
						a_out.push_back({ a_actor, visual.weapons[*shape], left ? "SHIELD" : "WEAPON", a_seconds, weapon->GetFormID(), left });
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
            if (a_wanted.bone && !bone) return;
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

		// Our own one-shots on someone: through the manager like Fight With Me's authored art, or, once the
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

		// With Fight With Me's authored art a look keeps only our rim under it; without, an aura is our own sparks
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
			return a_shader && (in(finalAuras) || in(rims) || in(auras) || in(flames) || in(elementals) || a_shader == Forms::Get().markGlow);
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
				list += std::format(" ({})", fellBack[i].empty() ? "Skyrim-based status shader"s : fellBack[i]);
			}
			if (themes > 0) {
				Log::Info("[Effects] {} look(s) use Fight With Me's authored art: {}."sv, themes, theme);
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
        if (!RE::BSResourceNiBinaryStream(R"(textures\FightWithMe\EffectsPack-1.txt)").good())
            Log::Warn("[Effects] Required POTC Effects pack (contract 1) is missing or outdated. Install/enable the separate Effects download."sv);
		LoadArt();
		auto* rim = handler ? handler->LookupForm<RE::TESEffectShader>(0x0FE68D, "Skyrim.esm"sv) : nullptr;    // GhostWolfSpiritFXShader
		auto* ritual = handler ? handler->LookupForm<RE::TESEffectShader>(0x10CDC9, "Skyrim.esm"sv) : nullptr;  // HealCircleFXS
		auto* flame = handler ? handler->LookupForm<RE::TESEffectShader>(0x0E7557, "Skyrim.esm"sv) : nullptr;   // TurnUndeadFlamesFXS
		int   made = 0;
		for (std::size_t i = 0; i < paints.size(); ++i) {
			const auto& paint = paints[i];
			rims[i] = rim ? Rim(rim, paint) : nullptr;
			finalAuras[i] = ritual ? Aura(ritual, paint) : nullptr;
			if (auto* aura = finalAuras[i]) {
				auto& d = aura->data;
				d.fillTextureEffectFullAlphaRatio = .22f;
				d.edgeEffectFullAlphaTime = .75f;
				d.edgeEffectPersistentAlphaRatio = .45f;
				d.particleShaderFullParticleBirthRatio *= 1.35f;
				d.particleShaderScaleKey1 *= 1.25f;
			}
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
		circleModel = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kCircleModel)).good();
		for (std::size_t i = 0; i < kRejuvenationModels.size(); ++i) {
			rejuvenationModels[i] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kRejuvenationModels[i])).good();
			if (!rejuvenationModels[i]) Log::Warn("[Effects] meshes\\{} is missing: Rejuvenation still restores magicka without its seal."sv, kRejuvenationModels[i]);
		}
		if (!circleModel) {
			Log::Warn("[Effects] meshes\\{} is missing: the healer's circle heals without showing."sv, kCircleModel);
		}
		for (std::size_t copy=0;copy<kBerserkModels.size();++copy) {
			berserkModels[copy]=RE::BSResourceNiBinaryStream(std::format("meshes\\{}",kBerserkModels[copy])).good();
			if(!berserkModels[copy])Log::Warn("[Effects] Berserk mantle copy {} missing; weapon flames remain available."sv,copy+1);
		}
		for (std::size_t copy=0;copy<kSurgeModels.size();++copy) {
			surgeModels[copy]=RE::BSResourceNiBinaryStream(std::format("meshes\\{}",kSurgeModels[copy])).good();
			if(!surgeModels[copy])Log::Warn("[Effects] Arcane Surge circle copy {} missing; caster aura remains available."sv,copy+1);
		}
        for(std::size_t i=0;i<riposteModels.size();++i){
            riposteModels[i]=std::format("FightWithMe\\RiposteStrike{}.nif",i?std::to_string(i+1):std::string{});
            riposteAvailable[i]=RE::BSResourceNiBinaryStream("meshes\\"+riposteModels[i]).good();
            if(!riposteAvailable[i])Log::Warn("[Effects] {} missing: Riposte uses stock sparks."sv,riposteModels[i]);
        }
        for(std::size_t i=0;i<windstepModels.size();++i){
            windstepModels[i]=std::format("FightWithMe\\Windstep{}{}.nif",i>=8?"Female":"",i%8?std::to_string(i%8+1):std::string{});
            windstepAvailable[i]=RE::BSResourceNiBinaryStream("meshes\\"+windstepModels[i]).good();
            if(!windstepAvailable[i])Log::Warn("[Effects] {} missing: windstep art is unavailable."sv,windstepModels[i]);
        }
        for(std::size_t i=0;i<arrowArtModels.size();++i){
            arrowArtModels[i]=std::format("FightWithMe\\ArrowArts{}.nif",i?std::to_string(i+1):std::string{});
            arrowArtAvailable[i]=RE::BSResourceNiBinaryStream("meshes\\"+arrowArtModels[i]).good();
            if(!arrowArtAvailable[i])Log::Warn("[Effects] {} missing: arrow art is unavailable."sv,arrowArtModels[i]);
        }
        for(std::size_t i=0;i<disarmModels.size();++i){
            disarmModels[i]=std::format("FightWithMe\\DisarmStrike{}.nif",i?std::to_string(i+1):std::string{});
            disarmAvailable[i]=RE::BSResourceNiBinaryStream("meshes\\"+disarmModels[i]).good();
            if(!disarmAvailable[i])Log::Warn("[Effects] {} missing: Disarm uses stock sparks."sv,disarmModels[i]);
        }
        for(std::size_t i=0;i<moonModels.size();++i){
            moonModels[i]=std::format("FightWithMe\\AzurasMoon{}.nif",i?std::to_string(i+1):std::string{});
            moonAvailable[i]=RE::BSResourceNiBinaryStream("meshes\\"+moonModels[i]).good();
        }
        for(std::size_t i=0;i<windModels.size();++i){
            windModels[i]=std::format("FightWithMe\\SecondWind{}.nif",i?std::to_string(i+1):std::string{});
            windAvailable[i]=RE::BSResourceNiBinaryStream("meshes\\"+windModels[i]).good();
            if(!windAvailable[i])Log::Warn("[Effects] {} missing: Second Wind uses its restorative aura."sv,windModels[i]);
        }

		for (std::size_t copy = 0; copy < kSunderModels.size(); ++copy) {
			sunderModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kSunderModels[copy])).good();
			if (!sunderModels[copy]) Log::Warn("[Effects] meshes\\{} is missing: Sunder uses its ordinary impact when no fracture copy is free."sv, kSunderModels[copy]);
		}
		for (std::size_t i = 0; i < kBlastModels.size(); ++i) {
			for (std::size_t copy = 0; copy < 2; ++copy) {
				blastModels[i][copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kBlastModels[i][copy])).good();
				if (!blastModels[i][copy]) {
					Log::Warn("[Effects] meshes\\{} is missing: Elemental Burst strikes without showing{}."sv, kBlastModels[i][copy], copy ? " right after another" : "");
				}
			}
		}
		orbModel = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kOrbModel)).good();
		if (!orbModel) {
			Log::Warn("[Effects] meshes\\{} is missing: Magnus holds without its orb showing."sv, kOrbModel);
		}
		quakeModel = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kQuakeModel)).good();
		if (!quakeModel) {
			Log::Warn("[Effects] meshes\\{} is missing: Earthshaker slams without its rocks showing."sv, kQuakeModel);
		}
		for (std::size_t copy = 0; copy < kCloakModels.size(); ++copy) {
			cloakModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kCloakModels[copy])).good();
			if (!cloakModels[copy]) {
				Log::Warn("[Effects] meshes\\{} is missing: Renewal heals without its cloak of water{}."sv, kCloakModels[copy], copy ? " while another healer wears one" : "");
			}
		}
		for (std::size_t copy = 0; copy < kRainModels.size(); ++copy) {
			rainModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kRainModels[copy])).good();
			if (!rainModels[copy]) {
				Log::Warn("[Effects] meshes\\{} is missing: Arrow Rain falls without its mark showing{}."sv, kRainModels[copy], copy ? " while another rain plays" : "");
			}
		}
		for (std::size_t copy = 0; copy < kHauntModels.size(); ++copy) {
			hauntModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kHauntModels[copy])).good();
			if (!hauntModels[copy]) {
				Log::Warn("[Effects] meshes\\{} is missing: Kynareth's Maw's catch stands frozen without its ghostly smoke{}."sv, kHauntModels[copy],
					copy ? " while another wolf's catch does" : "");
			}
		}
		for (std::size_t copy = 0; copy < kChaosModels.size(); ++copy) {
			chaosModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kChaosModels[copy])).good();
			chaosGrandModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kChaosGrandModels[copy])).good();
			if (!chaosModels[copy]) Log::Warn("[Effects] Chaos Down mesh {} missing; the slow and matching target auras still apply."sv, copy + 1);
		}
		for (std::size_t copy = 0; copy < kChaosBindingModels.size(); ++copy) {
			chaosBindingModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kChaosBindingModels[copy])).good();
			if (!chaosBindingModels[copy]) Log::Warn("[Effects] Chaos Down target aura {} missing; its slow remains active without the visual."sv, copy + 1);
		}
		for (std::size_t copy = 0; copy < kWeakWillModels.size(); ++copy) {
			weakWillModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kWeakWillModels[copy])).good();
			weakWillGrandModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kWeakWillGrandModels[copy])).good();
			if (!weakWillModels[copy]) Log::Warn("[Effects] Weak Will helmet {} missing; the ability still functions without its overlay."sv, copy + 1);
		}
		for (std::size_t i = 0; i < kBastionModels.size(); ++i)
			bastionModels[i] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kBastionModels[i])).good();
		const bool bearFur = RE::BSResourceNiBinaryStream(R"(textures\FightWithMe\SpectralFur.dds)").good();
		for (std::size_t copy = 0; copy < bearModels.size(); ++copy) {
			bearModels[copy] = nullptr;
			bearRedModels[copy] = nullptr;
			warSabreModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kWarSabre[copy])).good() ? kWarSabre[copy] : nullptr;
			warSabreRedModels[copy] = RE::BSResourceNiBinaryStream(std::format("meshes\\{}", kWarSabreRed[copy])).good() ? kWarSabreRed[copy] : nullptr;
			if (!warSabreModels[copy] || !warSabreRedModels[copy]) Log::Warn("[Effects] War Cry spirit mesh {} missing; ordinary wind cue remains."sv, copy + 1);
			for (const char* model : { bearFur ? kBearFurred[copy] : kBearPlain[copy], kBearPlain[copy] }) {
				if (RE::BSResourceNiBinaryStream(std::format("meshes\\{}", model)).good()) {
					bearModels[copy] = model;
					break;
				}
			}
			for (const char* model : { bearFur ? kBearRedFurred[copy] : kBearRedPlain[copy], kBearRedPlain[copy] }) {
				if (RE::BSResourceNiBinaryStream(std::format("meshes\\{}", model)).good()) { bearRedModels[copy] = model; break; }
			}
			if (!bearRedModels[copy]) Log::Warn("[Effects] Red rank III bear mesh {} missing."sv, copy + 1);
			if (!bearModels[copy]) Log::Warn("[Effects] Taunt bear mesh {} missing; ordinary taunt cue remains."sv, copy + 1);
		}
		constexpr std::array<RE::FormID, 3> hits{ 0x01B212, 0x01F03A, 0x057C67 };  // FireFXShader, FrostFXShader, ShockFXShader
		for (std::size_t i = 0; i < hits.size(); ++i) {
			const auto* base = handler ? handler->LookupForm<RE::TESEffectShader>(hits[i], "Skyrim.esm"sv) : nullptr;
			elementals[i] = base ? Copy(base) : nullptr;
		}
		{
			auto exists = [](const std::string& a_path) { return RE::BSResourceNiBinaryStream(a_path).good(); };
			const bool fur = exists(kFurTexture);
			for (const char* model : fur ? std::array{ kFurredHead, kPlainHead } : std::array{ kPlainHead, kFurredHead }) {
				if (exists(std::format("meshes\\{}", model)) && (model != kFurredHead || fur)) {
					headModel = model;
					break;
				}
			}
			if (headModel) {
				Log::Info("[Effects] Kynareth's Maw's head: meshes\\{} ({})."sv, headModel,
					headModel == kFurredHead ? "furred with Fight With Me's own texture"sv : (fur ? "the furred head is missing"sv : "Fight With Me's fur texture is missing"sv));
			} else {
				Log::Warn("[Effects] meshes\\{} is missing: Kynareth's Maw bites without its head showing."sv, kPlainHead);
			}
			for (const char* model : fur ? std::array{ kFurredRedHead, kPlainRedHead } : std::array{ kPlainRedHead, kFurredRedHead }) {
				if (exists(std::format("meshes\\{}", model)) && (model != kFurredRedHead || fur)) {
					redHeadModel = model;
					break;
				}
			}
			if (!redHeadModel) {
				Log::Warn("[Effects] meshes\\{} is missing: the third rank's wolf bites with the first ranks' head."sv, kPlainRedHead);
			}
		}
		constexpr std::array<RE::FormID, 3> blood{ 0x0F23C3, 0x0F457A, 0x0D30A4 };  // FXWerewolfFleshImpact, BloodSprayBleedImpactRed, WPNBlade2HandVsFleshImpactLG
		for (std::size_t i = 0; i < blood.size(); ++i) {
			const auto* source = handler ? handler->LookupForm<RE::BGSImpactData>(blood[i], "Skyrim.esm"sv) : nullptr;
			bloodModels[i] = source && source->GetModel() ? source->GetModel() : "";
			bloodSounds[i] = source ? source->sound1 : nullptr;
		}
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

	void FinalRank(RE::Actor* actor, Look look)
	{
		if (!actor || !Settings::Get().enabled || !Settings::Get().abilityEffects || !actor->Is3DLoaded() || actor->IsDead()) return;
		const auto index = static_cast<std::size_t>(look);
		if (index < finalAuras.size() && finalAuras[index]) actor->ApplyEffectShader(finalAuras[index], 1.8f);
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

	RE::BSTempEffectParticle* Head(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_heading, float a_seconds, bool a_grand)
	{
		auto*       cell = a_near ? a_near->GetParentCell() : nullptr;
		const char* model = a_grand && redHeadModel ? redHeadModel : headModel;
		if (!model || !Settings::Get().enabled || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const float   c = std::cos(a_heading), s = std::sin(a_heading);
		RE::NiMatrix3 facing;
		facing.entry[0][0] = c;
		facing.entry[0][1] = s;
		facing.entry[1][0] = -s;
		facing.entry[1][1] = c;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, model, facing, a_at, 1.0f, 0, nullptr);
		if (!effect) {
			return nullptr;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* BearHead(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds, bool a_grand, bool a_warCry)
	{
		const auto& bearPaths = a_warCry ? (a_grand ? warSabreRedModels : warSabreModels) : (a_grand ? bearRedModels : bearModels);
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= bearPaths.size() || !bearPaths[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, bearPaths[a_copy], still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

    RE::BSTempEffectParticle* BreathModel(RE::Actor* a_near, std::size_t copy, const RE::NiPoint3& at, float seconds, int part, int rank)
    {
        static constexpr std::array<std::array<const char*,4>,11> breathModels{{
        {R"(FightWithMe\DragonBreathHead.nif)",R"(FightWithMe\DragonBreathHead2.nif)",R"(FightWithMe\DragonBreathHead3.nif)",R"(FightWithMe\DragonBreathHead4.nif)"},
        {R"(FightWithMe\DragonBreathAlduin.nif)",R"(FightWithMe\DragonBreathAlduin2.nif)",R"(FightWithMe\DragonBreathAlduin3.nif)",R"(FightWithMe\DragonBreathAlduin4.nif)"},
        {R"(FightWithMe\BreathFire.nif)",R"(FightWithMe\BreathFire2.nif)",R"(FightWithMe\BreathFire3.nif)",R"(FightWithMe\BreathFire4.nif)"},
        {R"(FightWithMe\BreathFrost.nif)",R"(FightWithMe\BreathFrost2.nif)",R"(FightWithMe\BreathFrost3.nif)",R"(FightWithMe\BreathFrost4.nif)"},
        {R"(FightWithMe\BreathShock.nif)",R"(FightWithMe\BreathShock2.nif)",R"(FightWithMe\BreathShock3.nif)",R"(FightWithMe\BreathShock4.nif)"},
        {R"(FightWithMe\BreathFireII.nif)",R"(FightWithMe\BreathFireII2.nif)",R"(FightWithMe\BreathFireII3.nif)",R"(FightWithMe\BreathFireII4.nif)"},
        {R"(FightWithMe\BreathFrostII.nif)",R"(FightWithMe\BreathFrostII2.nif)",R"(FightWithMe\BreathFrostII3.nif)",R"(FightWithMe\BreathFrostII4.nif)"},
        {R"(FightWithMe\BreathShockII.nif)",R"(FightWithMe\BreathShockII2.nif)",R"(FightWithMe\BreathShockII3.nif)",R"(FightWithMe\BreathShockII4.nif)"},
        {R"(FightWithMe\BreathFireIII.nif)",R"(FightWithMe\BreathFireIII2.nif)",R"(FightWithMe\BreathFireIII3.nif)",R"(FightWithMe\BreathFireIII4.nif)"},
        {R"(FightWithMe\BreathFrostIII.nif)",R"(FightWithMe\BreathFrostIII2.nif)",R"(FightWithMe\BreathFrostIII3.nif)",R"(FightWithMe\BreathFrostIII4.nif)"},
        {R"(FightWithMe\BreathShockIII.nif)",R"(FightWithMe\BreathShockIII2.nif)",R"(FightWithMe\BreathShockIII3.nif)",R"(FightWithMe\BreathShockIII4.nif)"}
        }};
        auto* cell=a_near?a_near->GetParentCell():nullptr;
        if(part<0||part>=5||copy>=4||!Settings::Get().enabled||!Settings::Get().abilityEffects||!cell||!cell->IsAttached()) return nullptr;
        const int index=part<2?part:part+3*(std::clamp(rank,1,3)-1);
        const char* model=breathModels[index][copy];
        if(!RE::BSResourceNiBinaryStream(std::format("meshes\\{}",model)).good()) return nullptr;
        auto* effect=RE::BSTempEffectParticle::Spawn(cell,seconds,model,RE::NiMatrix3{},at,1.f,0,nullptr);
        if(effect) {const std::scoped_lock guard(groundSaveLock);ownedGroundEffects.insert(effect);heads.emplace_back(effect);}
        return effect;
    }

	RE::BSTempEffectParticle* ChaosGround(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds, bool a_grand)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		const auto& modelsForRank = a_grand ? chaosGrandModels : chaosModels;
		if (a_copy >= modelsForRank.size() || !modelsForRank[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, (a_grand ? kChaosGrandModels : kChaosModels)[a_copy], still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* ChaosBinding(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= kChaosBindingModels.size() || !chaosBindingModels[a_copy] || !Settings::Get().enabled ||
			!Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kChaosBindingModels[a_copy], still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* WeakWillHelmet(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds, bool a_grand)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		const auto& available = a_grand ? weakWillGrandModels : weakWillModels;
		if (a_copy >= available.size() || !available[a_copy] || !std::isfinite(a_seconds) || a_seconds <= 0 ||
			!Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, (a_grand ? kWeakWillGrandModels : kWeakWillModels)[a_copy], still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* BastionCastle(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_seconds, bool a_grand)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		const std::size_t model = a_grand ? 1 : 0;
		if (!bastionModels[model] || !std::isfinite(a_seconds) || a_seconds <= 0 || !Settings::Get().enabled ||
			!Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kBastionModels[model], still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{ const std::scoped_lock guard(groundSaveLock); ownedGroundEffects.insert(effect); }
		heads.emplace_back(effect); return effect;
	}

	RE::BSTempEffectParticle* Circle(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (!circleModel || !Settings::Get().enabled || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const RE::NiMatrix3 flat;
		auto*               effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kCircleModel, flat, a_at, 1.0f, 0, nullptr);
		if (!effect) {
			return nullptr;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* RejuvenationCircle(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= kRejuvenationModels.size() || !rejuvenationModels[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 flat;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kRejuvenationModels[a_copy], flat, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* Blast(RE::Actor* a_near, std::size_t a_element, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_element >= kBlastModels.size() || !Settings::Get().enabled || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const std::size_t copy = a_copy % 2 && blastModels[a_element][1] ? 1 : 0;
		if (!blastModels[a_element][copy]) {
			return nullptr;
		}
		const RE::NiMatrix3 flat;
		auto*               effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kBlastModels[a_element][copy], flat, a_at, 1.0f, 0, nullptr);
		if (!effect) {
			return nullptr;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* Quake(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (!quakeModel || !Settings::Get().enabled || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const RE::NiMatrix3 flat;
		auto*               effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kQuakeModel, flat, a_at, 1.0f, 0, nullptr);
		if (!effect) {
			return nullptr;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* BerserkMantle(RE::Actor* actor, std::size_t copy, const RE::NiPoint3& at, float seconds)
	{
		auto* cell=actor?actor->GetParentCell():nullptr;
		if(copy>=kBerserkModels.size()||!berserkModels[copy]||!Settings::Get().enabled||!Settings::Get().abilityEffects||!cell||!cell->IsAttached())return nullptr;
		const RE::NiMatrix3 still;
		auto* effect=RE::BSTempEffectParticle::Spawn(cell,seconds,kBerserkModels[copy],still,at,1.f,0,nullptr);
		if(!effect)return nullptr;
		{const std::scoped_lock guard(groundSaveLock);ownedGroundEffects.insert(effect);}
		heads.emplace_back(effect);return effect;
	}
	RE::BSTempEffectParticle* ArcaneSurgeCircle(RE::Actor* actor, std::size_t copy, const RE::NiPoint3& at, float seconds)
	{
		auto* cell=actor?actor->GetParentCell():nullptr;
		if(copy>=kSurgeModels.size()||!surgeModels[copy]||!Settings::Get().enabled||!Settings::Get().abilityEffects||!cell||!cell->IsAttached())return nullptr;
		const RE::NiMatrix3 still;
		auto* effect=RE::BSTempEffectParticle::Spawn(cell,seconds,kSurgeModels[copy],still,at,1.f,0,nullptr);
		if(!effect)return nullptr;
		{const std::scoped_lock guard(groundSaveLock);ownedGroundEffects.insert(effect);}
		heads.emplace_back(effect);return effect;
	}

	RE::BSTempEffectParticle* SunderBreak(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= kSunderModels.size() || !sunderModels[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kSunderModels[a_copy], still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* RiposteStrike(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= riposteModels.size() || !riposteAvailable[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, riposteModels[a_copy].c_str(), still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	void ArrowExplosion(RE::Actor* actor,const RE::NiPoint3& at,int rank)
	{
		auto* cell=actor?actor->GetParentCell():nullptr;
		if(!Settings::Get().enabled||!Settings::Get().abilityEffects||!cell||!cell->IsAttached())return;
		// Stock Skyrim fireball art under our own path; external fireball replacers cannot
		// substitute a different mesh. Its authored sequence must start after async 3D load.
		constexpr auto model=R"(FightWithMe\ArrowExplosion.nif)";
		const float scale=.85f+.25f*static_cast<float>(std::clamp(rank,1,3)-1);
		auto* effect=RE::BSTempEffectParticle::Spawn(cell,3.f,model,RE::NiMatrix3{},at,scale,0,nullptr);
		if(!effect){Log::Warn("[Explosive Arrow] Explosion art failed to load; check the Effects pack."sv);return;}
		{const std::scoped_lock guard(groundSaveLock);ownedGroundEffects.insert(effect);}
		groundBursts.push_back({RE::NiPointer<RE::BSTempEffectParticle>(effect),model});
		StartGroundAnimation(groundBursts.back());
	}

	RE::BSTempEffectParticle* Windstep(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		if (a_copy >= 8) return nullptr;
		const auto* base = a_near ? a_near->GetActorBase() : nullptr;
		const auto model = a_copy + (base && base->IsFemale() ? 8 : 0);
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (!windstepAvailable[model] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, windstepModels[model].c_str(), still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* ArrowArt(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= arrowArtModels.size() || !arrowArtAvailable[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, arrowArtModels[a_copy].c_str(), still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* DisarmStrike(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= disarmModels.size() || !disarmAvailable[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, disarmModels[a_copy].c_str(), still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* SecondWindBurst(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= windModels.size() || !windAvailable[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, windModels[a_copy].c_str(), still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}
	RE::BSTempEffectParticle* Moon(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (a_copy >= moonModels.size() || !moonAvailable[a_copy] || !Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached()) return nullptr;
		const RE::NiMatrix3 still;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, moonModels[a_copy].c_str(), still, a_at, 1.0f, 0, nullptr);
		if (!effect) return nullptr;
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* Cloak(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		const std::size_t copy = a_copy % 2 && cloakModels[1] ? 1 : 0;
		if (!cloakModels[copy] || !Settings::Get().enabled || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const RE::NiMatrix3 still;
		auto*               effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kCloakModels[copy], still, a_at, 1.0f, 0, nullptr);
		if (!effect) {
			return nullptr;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* Haunt(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto*             cell = a_near ? a_near->GetParentCell() : nullptr;
		const std::size_t copy = a_copy % 2 && hauntModels[1] ? 1 : 0;
		if (!hauntModels[copy] || !Settings::Get().enabled || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const RE::NiMatrix3 still;
		auto*               effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kHauntModels[copy], still, a_at, 1.0f, 0, nullptr);
		if (!effect) {
			return nullptr;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* Rain(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		const std::size_t copy = a_copy % 2 && rainModels[1] ? 1 : 0;
		if (!rainModels[copy] || !Settings::Get().enabled || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const RE::NiMatrix3 flat;
		auto*               effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kRainModels[copy], flat, a_at, 1.0f, 0, nullptr);
		if (!effect) {
			return nullptr;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	RE::BSTempEffectParticle* Orb(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_seconds)
	{
		auto* cell = a_near ? a_near->GetParentCell() : nullptr;
		if (!orbModel || !Settings::Get().enabled || !cell || !cell->IsAttached()) {
			return nullptr;
		}
		const RE::NiMatrix3 still;
		auto*               effect = RE::BSTempEffectParticle::Spawn(cell, a_seconds, kOrbModel, still, a_at, 1.0f, 0, nullptr);
		if (!effect) {
			return nullptr;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		heads.emplace_back(effect);
		return effect;
	}

	void Struck(RE::Actor* a_actor, std::size_t a_element, float a_seconds)
	{
		if (!a_actor || a_element >= elementals.size() || !elementals[a_element] || !Settings::Get().enabled || !Settings::Get().abilityEffects ||
			!a_actor->Is3DLoaded() || a_actor->IsDead()) {
			return;
		}
		a_actor->ApplyEffectShader(elementals[a_element], a_seconds);
	}

	RE::BSTempEffectParticle* ArrowTrail(RE::TESObjectCELL* cell, const RE::NiPoint3& at, std::size_t element)
	{
		constexpr std::array arrowModels{ R"(FightWithMe\SpellArrowFire.nif)", R"(FightWithMe\SpellArrowFrost.nif)", R"(FightWithMe\SpellArrowShock.nif)" };
		if (element >= arrowModels.size() || !cell || !cell->IsAttached() || !Settings::Get().enabled || !Settings::Get().abilityEffects) return nullptr;
		RE::NiMatrix3 facing;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, 12.5f, arrowModels[element], facing, at, 1.0f, 0, nullptr);
		if (effect) {
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
			heads.emplace_back(effect);
		}
		return effect;
	}

	void Spray(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_heading, Blood a_kind, float a_scale)
	{
		const auto kind = static_cast<std::size_t>(a_kind);
		auto*      cell = a_near ? a_near->GetParentCell() : nullptr;
		if (!Settings::Get().enabled || !Settings::Get().abilityEffects || !cell || !cell->IsAttached() || bloodModels[kind].empty()) {
			return;
		}
		// Turned to the bite's heading, in case the spray has a forward.
		const float   c = std::cos(a_heading), s = std::sin(a_heading);
		RE::NiMatrix3 facing;
		facing.entry[0][0] = c;
		facing.entry[0][1] = s;
		facing.entry[1][0] = -s;
		facing.entry[1][1] = c;
		auto* effect = RE::BSTempEffectParticle::Spawn(cell, 2.0f, bloodModels[kind].c_str(), facing, a_at, a_scale, 0, nullptr);
		if (!effect) {
			return;
		}
		{
			const std::scoped_lock guard(groundSaveLock);
			ownedGroundEffects.insert(effect);
		}
		sprays.emplace_back(effect);
		RE::BSSoundHandle sound;
		if (auto* audio = RE::BSAudioManager::GetSingleton(); a_kind == Blood::kBite && audio && bloodSounds[kind] && audio->GetSoundHandle(sound, bloodSounds[kind])) {
			sound.SetPosition(a_at);
			sound.Play();
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

	namespace
	{
		// Every look and garnish out; Kynareth's Maw's heads stay, being the ability itself.
		void ClearLooks()
		{
			for (auto& burst : groundBursts) {
				burst.effect->lifetime = 0.0f;
				ForgetGroundEffect(burst.effect.get());
			}
			groundBursts.clear();
			for (auto& spray : sprays) {
				spray->lifetime = 0.0f;
				ForgetGroundEffect(spray.get());
			}
			sprays.clear();
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

		bool Expired(RE::NiPointer<RE::BSTempEffectParticle>& a_effect)
		{
			if (!a_effect->cell || !a_effect->cell->IsAttached() || a_effect->age >= a_effect->lifetime) {
				a_effect->lifetime = 0.0f;
				ForgetGroundEffect(a_effect.get());
				return true;
			}
			return false;
		}
	}

	void Clear()
	{
		ClearLooks();
		for (auto& head : heads) {
			head->lifetime = 0.0f;
			ForgetGroundEffect(head.get());
		}
		heads.clear();
	}

    void Animate()
    {
        if (!Settings::Get().enabled || !Settings::Get().abilityEffects) return;
        for (auto& model : models) {
            if (model.effect->finished || !Valid(model)) continue;
            auto* root = model.effect->Get3D();
            if (!root) continue;
            if (model.animatedRoot.get() != root) {
                model.animatedRoot.reset(root);
                constexpr std::array labels{ "FWM_Core", "FWM_Wisps", "FWM_Motes" };
                for (std::size_t i=0; i<labels.size(); ++i) {
                    model.parts[i] = root->GetObjectByName(labels[i]);
                    model.materials[i] = i < 2 ? Posing::ShadersOf(model.parts[i]) : std::vector<RE::BSShaderProperty*>{};
                }
                for (std::size_t i=0; i<model.motes.size(); ++i) {
                    auto& mote=model.motes[i];
                    mote={};
                    mote.node=root->GetObjectByName(std::format("MoteAt{}",i));
                    if (mote.node) { mote.origin=mote.node->local.translate; mote.materials=Posing::ShadersOf(mote.node); }
                }
            }
            const auto kind = SupportKind(model.look);
            const float age=model.effect->age, remaining=model.effect->lifetime-age;
            const auto frame = SupportArt::At(age, remaining, kind);
            const auto& p = paints[static_cast<std::size_t>(model.look)];
            std::array<float, 3> color{p.r/255.0f, p.g/255.0f, p.b/255.0f};
            if (model.weapon) {
                if (model.look == Look::kBerserk) color = {1.0f,.72f,.24f};
                else if (kind == SupportArt::Kind::Steel) color = {.8f,.87f,1.0f};
                else color = {.3f,.65f,1.0f};
            }
            for (std::size_t i=0; i<model.parts.size(); ++i) {
                auto* part = model.parts[i];
                if (!part) continue;
                Posing::Paint(model.materials[i], color, frame.alpha);
                if (i < 2) {
                    Posing::Drift(model.materials[i], i==0 ? frame.coreDrift : frame.drift);
                    const float angle=i==0 ? frame.coreTurn : frame.turn;
                    const float c=std::cos(angle), sn=std::sin(angle);
                    part->local.rotate.entry[0][0]=c; part->local.rotate.entry[0][1]=-sn;
                    part->local.rotate.entry[1][0]=sn; part->local.rotate.entry[1][1]=c;
                    part->local.scale = i==0 ? frame.coreScale : frame.scale;
                }
                Posing::Refresh(part);
            }
            for (std::size_t i=0; i<SupportArt::MoteCount(kind); ++i) {
                auto& mote=model.motes[i];
                if (!mote.node) continue;
                const auto motion=SupportArt::Particle(age,remaining,kind,i);
                const float c=std::cos(motion.turn), sn=std::sin(motion.turn);
                mote.node->local.translate={c*mote.origin.x-sn*mote.origin.y, sn*mote.origin.x+c*mote.origin.y, mote.origin.z+motion.rise};
                mote.node->local.scale=motion.scale;
                Posing::Paint(mote.materials, color, motion.alpha);
                Posing::Refresh(mote.node);
            }
        }
    }

	void Update()
	{
		std::erase_if(heads, Expired);
		if (!Settings::Get().abilityEffects || !Settings::Get().enabled) {
			ClearLooks();
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
		std::erase_if(sprays, Expired);
		std::erase_if(models, [](Model& model) {
			if (!Valid(model) || (model.effect->lifetime >= 0.0f && model.effect->age >= model.effect->lifetime)) {
				model.effect->finished = true;
			}
			return model.effect->finished;
		});
	}
}

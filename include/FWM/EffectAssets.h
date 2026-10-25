#pragma once
#include "FWM/Effects.h"

// All supporting art is authored by Fight With Me. No external effect plugin is queried.
namespace FWM::Effects::Assets
{
    struct Art { Look look; const char* model; const char* bone{ "NPC Root [Root]" }; float groundSeconds{ 0.0f }; };
    inline constexpr std::array art{
        Art{ Look::kBraced, R"(FightWithMe\Support\Guard.nif)" },
        Art{ Look::kTaunt, R"(FightWithMe\Support\Wind.nif)" },
        Art{ Look::kVolley, R"(FightWithMe\Support\Focus.nif)" },
        Art{ Look::kQuarry, R"(FightWithMe\Support\Mark.nif)" },
        Art{ Look::kGuard, R"(FightWithMe\Support\Guard.nif)" },
        Art{ Look::kHealAura, R"(FightWithMe\Support\Vitality.nif)" },
        Art{ Look::kRejuvenateAura, R"(FightWithMe\Support\Vitality.nif)" },
        Art{ Look::kWardAura, R"(FightWithMe\Support\Guard.nif)" },
        Art{ Look::kRally, R"(FightWithMe\Support\Rally.nif)" },
        Art{ Look::kPinned, R"(FightWithMe\Support\Binding.nif)" },
        Art{ Look::kFocus, R"(FightWithMe\Support\Focus.nif)" },
        Art{ Look::kSurge, R"(FightWithMe\Support\Surge.nif)" },
        Art{ Look::kWarCry, R"(FightWithMe\Support\Wind.nif)" },
        Art{ Look::kSunderImpact, R"(FightWithMe\Support\Impact.nif)" },
        Art{ Look::kRenewal, R"(FightWithMe\Support\Vitality.nif)" },
        Art{ Look::kSecondWind, R"(FightWithMe\Support\Vitality.nif)" },
        Art{ Look::kLevelUp, R"(FightWithMe\Support\LevelUp.nif)" }
    };
    struct Weapon { Look look; const char* element; };
    inline constexpr std::array weapons{
        Weapon{ Look::kStrike, "Steel" }, Weapon{ Look::kBerserk, "Fire" },
        Weapon{ Look::kSpiritWolf, "Spirit" }, Weapon{ Look::kRiposte, "Steel" }
    };
    // Indices match WeaponShape(); left/right share geometry, not an attachment bone.
    inline constexpr std::array weaponShapes{
        "Sword", "Sword", "Dagger", "Dagger", "WarAxe", "WarAxe",
        "Mace", "Mace", "Greatsword", "Battleaxe", "Warhammer"
    };
}

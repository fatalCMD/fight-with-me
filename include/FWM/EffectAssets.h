#pragma once

#include "FWM/Effects.h"

// Paths in the required mods, not files shipped by Fight With Me. Keep these explicit:
// tools/VerifyEffectAssets.py checks them against the installed plugins and archives.
namespace FWM::Effects::Assets
{
	enum class Source : std::uint8_t { kTriumvirate, kElemental, kVibrant, kCount };
	inline constexpr std::array plugins{
		"Triumvirate - Mage Archetypes.esp", "Elemental Destruction Magic Redux.esp", "Vibrant weapons.esl"
	};
	struct Art
	{
		Look look;
		Source source;
		const char* model;
		const char* bone{ nullptr };  // null: use the mesh's own attachment instructions
		float groundSeconds{ 0.0f };  // area-effect mesh: spawn in the cell, not as actor hit art
	};
	inline constexpr std::array art{
		Art{ Look::kBraced, Source::kTriumvirate, R"(Triumvirate\Auras_Art_Buff_1.nif)" },
		Art{ Look::kTaunt, Source::kElemental, R"(magic\windexp01.nif)", nullptr, 2.3f },
		Art{ Look::kVolley, Source::kTriumvirate, R"(Diviner\illusiondiviner_art_buffshield.nif)" },
		Art{ Look::kQuarry, Source::kTriumvirate, R"(Triumvirate\Primal_Art_HuntersMark.nif)" },
		Art{ Look::kGuard, Source::kTriumvirate, R"(Triumvirate\Auras_Art_Barrier.nif)" },
		Art{ Look::kHealAura, Source::kTriumvirate, R"(ApocNew\RestorationPeace_Art_WildHealing.nif)" },
		Art{ Look::kRejuvenateAura, Source::kElemental, R"(magic\fxwatercloak01.nif)" },
		Art{ Look::kWardAura, Source::kTriumvirate, R"(Triumvirate\Auras_Art_Barrier.nif)" },
		Art{ Look::kRally, Source::kTriumvirate, R"(Triumvirate\Auras_Art_Buff_3.nif)" },
		Art{ Look::kPinned, Source::kTriumvirate, R"(Triumvirate\Violence_Art_Chained.nif)" },
		Art{ Look::kFocus, Source::kTriumvirate, R"(Diviner\illusiondiviner_art_buffshield.nif)" },
		Art{ Look::kSurge, Source::kTriumvirate, R"(Triumvirate\Rift_Art_Buff.nif)" },
		Art{ Look::kMagnus, Source::kTriumvirate, R"(Triumvirate\Auras_Art_Resurrect.nif)" },
		Art{ Look::kWarCry, Source::kElemental, R"(magic\windexp01.nif)", nullptr, 2.3f },
		Art{ Look::kEarthshaker, Source::kElemental, R"(magic\EarthRockExplosion01.nif)", nullptr, 7.0f },
		Art{ Look::kSunderImpact, Source::kElemental, R"(effects\impacteffects\impactrock02.nif)", "NPC Spine2 [Spn2]" },
		Art{ Look::kRenewal, Source::kTriumvirate, R"(Triumvirate\Violence_Art_HealingTrance.nif)" },
		Art{ Look::kSecondWind, Source::kTriumvirate, R"(Triumvirate\Auras_Art_Buff_2.nif)" },
		Art{ Look::kStandFast, Source::kTriumvirate, R"(Triumvirate\Violence_Art_Buffshield_1.nif)" },
		Art{ Look::kChallenge, Source::kTriumvirate, R"(Triumvirate\Senua_Art_Runes_Persistent.nif)" },
		Art{ Look::kArrowRain, Source::kElemental, R"(magic\fxwindcloak01.nif)" }
	};
	struct Weapon
	{
		Look look;
		const char* element;
	};
	inline constexpr std::array weapons{
		Weapon{ Look::kStrike, "Shock" },
		Weapon{ Look::kBerserk, "Fire" },
		Weapon{ Look::kCleave, "FireShock" },
		Weapon{ Look::kRiposte, "Frost" }
	};
	// The left meshes attach themselves to SHIELD, right/two-handed ones to WEAPON.
	// Use the matching hand and weapon shape; never attach a sword effect to a bow or staff.
	inline constexpr std::array weaponShapes{
		"SwordR", "SwordL", "DaggerR", "DaggerL", "WarAxeR", "WarAxeL",
		"MaceR", "MaceL", "Greatsword", "Battleaxe", "Warhammer"
	};
}

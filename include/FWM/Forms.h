#pragma once

// Forms looked up once at data load. Anything the player might want to swap is
// read from the INI as "Plugin.esp|0xLocalID".

namespace FWM::Forms
{
	struct Set
	{
		RE::TESEffectShader*        markGlow{ nullptr };
		RE::BGSSoundDescriptorForm* tauntSound{ nullptr };
		RE::BGSSoundDescriptorForm* bearRoar{ nullptr };  // NPCBearAggroWarning02, Taunt's spectral bear
		RE::TESIdleForm*            idleShield{ nullptr };
		RE::TESIdleForm*            idleWeapon{ nullptr };
		RE::TESIdleForm*            idleCry{ nullptr };
		RE::TESImageSpaceModifier*  imodIntro{ nullptr };
		RE::TESImageSpaceModifier*  imodHold{ nullptr };

		RE::BGSKeyword*         dragon{ nullptr };       // ActorTypeDragon
		RE::BGSKeyword*         restoreHealth{ nullptr };  // MagicRestoreHealth
		RE::BGSLocationRefType* boss{ nullptr };
		RE::TESFaction*         currentFollower{ nullptr };
		RE::TESCombatStyle*     defaultStyle{ nullptr };

		RE::TESFaction* nffRole{ nullptr };  // nwsFF_RoleFac: 0 Standard, 1 Healer, 2 Tank

		// A healer's or mage's ability: a gesture with magic in hand, the absorb spells' green or
		// blue glow in both hands, and the restoration or ward sound.
		RE::TESIdleForm*            idleMagic{ nullptr };        // IdleMagic_01
		RE::BGSArtObject*           handsGreen{ nullptr };       // AbsorbGreenHandFX01
		RE::BGSArtObject*           handsBlue{ nullptr };        // AbsorbBlueHandFX01
		RE::BGSSoundDescriptorForm* healSound{ nullptr };        // MAGRestorationFFFireSD
		RE::BGSSoundDescriptorForm* rejuvenateSound{ nullptr };  // MAGWardTestCastSD

		// Paths abilities
		RE::BGSKeyword*               undead{ nullptr };  // ActorTypeUndead: the healer's circle burns them
		// Elemental Burst: [element][tier], fire, frost, shock by Apprentice, Adept, Expert
		std::array<std::array<RE::SpellItem*, 3>, 3> bursts{};
		RE::SpellItem*                flameCloak{ nullptr };
		RE::TESAmmo*                  ironArrow{ nullptr };  // seed arrow for an empty archer-path quiver
		std::array<RE::TESAmmo*, 3>    pathArrows{};  // steel, elven, ebony: Fine Arrows
		RE::BGSSoundDescriptorForm*   quakeSound{ nullptr };  // a giant's club on dirt: Earthshaker
		RE::BGSSoundDescriptorForm*   quakeRocks{ nullptr };  // a rockfall trap let go, as Earthshaker's rocks burst out
		RE::BGSSoundDescriptorForm*   quakeRumble{ nullptr }; // the ground's rumble and shake under it
		RE::BGSSoundDescriptorForm*   quakeCrumble{ nullptr };  // a cave crumbling, as they sink back
		std::array<RE::BGSSoundDescriptorForm*, 4> bastionSounds{};  // Bastion::kSounds order
		std::array<RE::BGSSoundDescriptorForm*, 4> bastionGrandSounds{};
		RE::BGSSoundDescriptorForm*   blowSound{ nullptr };   // a two-handed blow landing: Sunder, the spectral wolf's bite
		RE::BGSSoundDescriptorForm*   wolfSnarl{ nullptr };   // NPCWolfAttackPower, as its jaws start to close
		RE::BGSSoundDescriptorForm*   wolfHowl{ nullptr };    // NPCWolfHowl, as the spectral wolf's head appears
		// Elemental Burst's blast by element (fire, frost, shock): as it goes off, and a moment later
		// (the fireball's roll, the ice shattering, the crackle of the second strike).
		std::array<RE::BGSSoundDescriptorForm*, 3> blastSounds{};
		std::array<RE::BGSSoundDescriptorForm*, 3> blastAfter{};
		// Magnus's orb: the Eye of Magnus opening, its low hum while it floats, and its vanishing.
		RE::BGSSoundDescriptorForm*   magnusOpen{ nullptr };
		RE::BGSSoundDescriptorForm*   magnusHum{ nullptr };
		RE::BGSSoundDescriptorForm*   magnusVanish{ nullptr };

		// Specials
		std::array<RE::SpellItem*, 4> flesh{};   // Oakflesh, Stoneflesh, Ironflesh, Ebonyflesh
		std::array<RE::TESEffectShader*, 4> fleshShaders{};  // the bark, stone, iron and ebony skins they show
	};

	void Resolve();

	[[nodiscard]] const Set& Get() noexcept;
}

#pragma once

// Forms looked up once at data load. Anything the player might want to swap is
// read from the INI as "Plugin.esp|0xLocalID".

namespace FWM::Forms
{
	struct Set
	{
		RE::TESEffectShader*        markGlow{ nullptr };
		RE::BGSSoundDescriptorForm* tauntSound{ nullptr };
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
		RE::BGSKeyword*               undead{ nullptr };  // ActorTypeUndead: Sun's Bane burns them
		// Elemental Burst: [element][tier], fire, frost, shock by Apprentice, Adept, Expert
		std::array<std::array<RE::SpellItem*, 3>, 3> bursts{};
		RE::SpellItem*                flameCloak{ nullptr };
		RE::TESAmmo*                  ironArrow{ nullptr };  // what a fletcher makes with nothing else to copy
		RE::BGSSoundDescriptorForm*   quakeSound{ nullptr };  // a giant's club on dirt: Earthshaker
		RE::BGSSoundDescriptorForm*   cleaveSound{ nullptr };  // a two-handed blow landing: Cleave, Sunder
		std::array<RE::BGSSoundDescriptorForm*, 2> chicken{};  // a cluck and a squawk, for companions with unlimited points

		// Specials
		std::array<RE::SpellItem*, 4> flesh{};   // Oakflesh, Stoneflesh, Ironflesh, Ebonyflesh
		std::array<RE::TESEffectShader*, 4> fleshShaders{};  // the bark, stone, iron and ebony skins they show
	};

	void Resolve();

	[[nodiscard]] const Set& Get() noexcept;
}

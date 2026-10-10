#pragma once

// Forms looked up once at data load. Anything the player might want to swap is
// read from the INI as "Plugin.esp|0xLocalID".

namespace FWM::Forms
{
	struct Set
	{
		RE::TESEffectShader*        glow{ nullptr };
		RE::BGSSoundDescriptorForm* tauntSound{ nullptr };
		RE::TESIdleForm*            idleShield{ nullptr };
		RE::TESIdleForm*            idleWeapon{ nullptr };
		RE::TESIdleForm*            idleCry{ nullptr };
		RE::SpellItem*              fallbackHeal{ nullptr };
		RE::TESImageSpaceModifier*  imodIntro{ nullptr };
		RE::TESImageSpaceModifier*  imodHold{ nullptr };

		RE::BGSKeyword*         dragon{ nullptr };       // ActorTypeDragon
		RE::BGSKeyword*         restoreHealth{ nullptr };  // MagicRestoreHealth
		RE::BGSLocationRefType* boss{ nullptr };
		RE::TESFaction*         currentFollower{ nullptr };
		RE::TESCombatStyle*     defaultStyle{ nullptr };

		RE::TESFaction* nffRole{ nullptr };  // nwsFF_RoleFac: 0 Standard, 1 Healer, 2 Tank
	};

	void Resolve();

	[[nodiscard]] const Set& Get() noexcept;
}

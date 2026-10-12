#pragma once

// Forms looked up once at data load. Anything the player might want to swap is
// read from the INI as "Plugin.esp|0xLocalID".

namespace FWM::Forms
{
	struct Set
	{
		RE::TESEffectShader*        glow{ nullptr };
		RE::TESEffectShader*        markGlow{ nullptr };
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

		// Specials
		std::array<RE::SpellItem*, 4> flesh{};   // Oakflesh, Stoneflesh, Ironflesh, Ebonyflesh
		std::array<RE::TESEffectShader*, 4> fleshShaders{};  // the bark, stone, iron and ebony skins they show
		std::array<RE::SpellItem*, 3> disarm{};  // VoiceDisarm1-3

		// Potions, weakest first. Restore: Minor to Ultimate; resist: 25 to 100%;
		// fortify: 01 to 05.
		std::array<RE::AlchemyItem*, 6> healingPotions{}, magickaPotions{}, staminaPotions{};
		std::array<RE::AlchemyItem*, 4> resistFire{}, resistFrost{}, resistShock{}, resistMagic{};
		std::array<RE::AlchemyItem*, 5> fortifyHealth{}, fortifyMagicka{}, fortifyStamina{}, fortifyHealRate{};
		RE::TESIdleForm*                idleDrink{ nullptr };
		RE::TESIdleForm*                idleBrew{ nullptr };      // IdleAlchemyEnter
		RE::TESIdleForm*                idleBrewDone{ nullptr };  // IdleAlchemyExit
	};

	void Resolve();

	[[nodiscard]] const Set& Get() noexcept;
}

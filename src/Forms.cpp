#include "FWM/Forms.h"

#include "FWM/Log.h"
#include "FWM/Settings.h"

namespace FWM::Forms
{
	namespace
	{
		Set forms;

		template <class T>
		T* Lookup(std::string_view a_plugin, RE::FormID a_local)
		{
			auto* handler = RE::TESDataHandler::GetSingleton();
			return handler ? handler->LookupForm<T>(a_local, a_plugin) : nullptr;
		}

		// "Plugin.esp|0x123ABC". An empty string turns the effect off on purpose.
		template <class T>
		T* FromSpec(std::string_view a_spec, std::string_view a_what)
		{
			if (a_spec.empty()) {
				return nullptr;
			}
			const auto bar = a_spec.find('|');
			if (bar == std::string_view::npos) {
				Log::Warn("{}: '{}' is not 'Plugin|0xID'."sv, a_what, a_spec);
				return nullptr;
			}
			const std::string plugin(a_spec.substr(0, bar));
			const std::string id(a_spec.substr(bar + 1));
			char*             end = nullptr;
			const auto        local = static_cast<RE::FormID>(std::strtoul(id.c_str(), &end, 0));
			auto*             form = end != id.c_str() ? Lookup<T>(plugin, local) : nullptr;
			if (!form) {
				Log::Warn("{}: {} not found or the wrong type."sv, a_what, a_spec);
			}
			return form;
		}
	}

	namespace
	{
		// The mark outline: a copy of a rim shader (no fill, only the edge, drawn through
		// walls), its edge turned a warm white so it reads apart from the taunt's red glow
		// and matches the plan's lock-on brackets. Made once; never saved, since a copy made
		// at runtime has no form ID for a save to point at.
		RE::TESEffectShader* Outline(RE::TESEffectShader* a_base)
		{
			if (!a_base) {
				return nullptr;
			}
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESEffectShader>();
			auto* outline = factory ? static_cast<RE::TESEffectShader*>(factory->Create()) : nullptr;
			if (!outline) {
				return a_base;
			}
			outline->data = a_base->data;
			outline->fillTexture.textureName = a_base->fillTexture.textureName;
			outline->particleShaderTexture.textureName = a_base->particleShaderTexture.textureName;
			outline->holesTexture.textureName = a_base->holesTexture.textureName;
			outline->membranePaletteTexture.textureName = a_base->membranePaletteTexture.textureName;
			outline->particlePaletteTexture.textureName = a_base->particlePaletteTexture.textureName;
			auto& d = outline->data;
			d.edgeEffectColor.red = 255;
			d.edgeEffectColor.green = 232;
			d.edgeEffectColor.blue = 190;
			d.edgeEffectFallOff = std::max(d.edgeEffectFallOff, 2.5f);  // a thinner rim
			d.edgeEffectPersistentAlphaRatio = 0.9f;
			d.edgeEffectFullAlphaRatio = 1.0f;
			d.edgeEffectAlphaPulseAmplitude = 0.1f;
			d.edgeEffectAlphaPulseFrequency = 1.5f;
			d.edgeEffectAlphaFadeInTime = 0.1f;
			d.edgeEffectAlphaFadeOutTime = 0.2f;
			return outline;
		}
	}

	void Resolve()
	{
		const auto& s = Settings::Get();
		forms.glow = FromSpec<RE::TESEffectShader>(s.glowShader, "Taunt glow"sv);
		forms.markGlow = Outline(FromSpec<RE::TESEffectShader>(s.markShader, "Mark outline"sv));
		forms.tauntSound = FromSpec<RE::BGSSoundDescriptorForm>(s.tauntSound, "Taunt sound"sv);
		forms.idleShield = FromSpec<RE::TESIdleForm>(s.idleShield, "Shield taunt"sv);
		forms.idleWeapon = FromSpec<RE::TESIdleForm>(s.idleWeapon, "Weapon taunt"sv);
		forms.idleCry = FromSpec<RE::TESIdleForm>(s.idleCry, "War cry"sv);
		forms.fallbackHeal = FromSpec<RE::SpellItem>(s.fallbackSpell, "Fallback heal"sv);
		forms.imodIntro = FromSpec<RE::TESImageSpaceModifier>(s.imodIntro, "Time effect intro"sv);
		forms.imodHold = FromSpec<RE::TESImageSpaceModifier>(s.imodHold, "Time effect"sv);

		forms.dragon = Lookup<RE::BGSKeyword>("Skyrim.esm"sv, 0x035D59);
		forms.restoreHealth = Lookup<RE::BGSKeyword>("Skyrim.esm"sv, 0x01CEB0);
		forms.boss = Lookup<RE::BGSLocationRefType>("Skyrim.esm"sv, 0x0130F7);
		forms.currentFollower = Lookup<RE::TESFaction>("Skyrim.esm"sv, 0x05C84E);
		forms.defaultStyle = Lookup<RE::TESCombatStyle>("Skyrim.esm"sv, 0x00003D);

		forms.nffRole = Lookup<RE::TESFaction>("nwsFollowerFramework.esp"sv, 0x0094CC);

		auto spells = [](auto& a_out, std::initializer_list<RE::FormID> a_ids) {
			std::size_t i = 0;
			for (const auto id : a_ids) {
				a_out[i++] = Lookup<RE::SpellItem>("Skyrim.esm"sv, id);
			}
		};
		auto potions = [](auto& a_out, std::initializer_list<RE::FormID> a_ids) {
			std::size_t i = 0;
			for (const auto id : a_ids) {
				a_out[i++] = Lookup<RE::AlchemyItem>("Skyrim.esm"sv, id);
			}
		};
		spells(forms.flesh, { 0x05AD5C, 0x05AD5D, 0x051B16, 0x05AD5E });
		spells(forms.disarm, { 0x08BB27, 0x08BB28, 0x08BB29 });
		forms.fleshShaders = { Lookup<RE::TESEffectShader>("Skyrim.esm"sv, 0x094160), Lookup<RE::TESEffectShader>("Skyrim.esm"sv, 0x094161),
			Lookup<RE::TESEffectShader>("Skyrim.esm"sv, 0x094163), Lookup<RE::TESEffectShader>("Skyrim.esm"sv, 0x094162) };
		potions(forms.healingPotions, { 0x03EADD, 0x03EADE, 0x03EADF, 0x03EAE3, 0x039BE4, 0x039BE5 });
		potions(forms.magickaPotions, { 0x03EAE0, 0x03EAE1, 0x03EAE2, 0x03EAE4, 0x039BE6, 0x039BE7 });
		potions(forms.staminaPotions, { 0x03EAE5, 0x039BE8, 0x03EAE7, 0x03EAE8, 0x03EAE6, 0x039CF3 });
		potions(forms.resistFire, { 0x039B4A, 0x03EAE9, 0x039B4B, 0x03EAEF });
		potions(forms.resistFrost, { 0x039B8A, 0x03EAED, 0x039BDF, 0x03EAF0 });
		potions(forms.resistShock, { 0x039BE1, 0x03EAEE, 0x039BE3, 0x03EAF1 });
		potions(forms.resistMagic, { 0x039E52, 0x039E53, 0x039E54, 0x039E55 });
		potions(forms.fortifyHealth, { 0x03EAF2, 0x03EAF4, 0x03EAF6, 0x03EAF5, 0x0FF9FC });
		potions(forms.fortifyMagicka, { 0x03EAF7, 0x03EAFA, 0x03EAFB, 0x03EAFC, 0x0FF9FD });
		potions(forms.fortifyStamina, { 0x03EAFD, 0x03EB00, 0x03EAFE, 0x03EAFF, 0x0FFA01 });
		potions(forms.fortifyHealRate, { 0x03EB09, 0x03EB0A, 0x03EB0B, 0x03EB0C, 0x0FF9FF });
		forms.idleDrink = Lookup<RE::TESIdleForm>("Skyrim.esm"sv, 0x0D33B0);
		forms.idleBrew = Lookup<RE::TESIdleForm>("Skyrim.esm"sv, 0x02A448);
		forms.idleBrewDone = Lookup<RE::TESIdleForm>("Skyrim.esm"sv, 0x02A449);
		const auto found = [](const auto& a_list) { return std::count_if(a_list.begin(), a_list.end(), [](auto* a_form) { return a_form != nullptr; }); };
		Log::Info("Forms: flesh {}/4, disarm {}/3, healing potions {}/6, resists {}/16, drink idle {}, brew idles {}/{}."sv, found(forms.flesh),
			found(forms.disarm), found(forms.healingPotions),
			found(forms.resistFire) + found(forms.resistFrost) + found(forms.resistShock) + found(forms.resistMagic), forms.idleDrink != nullptr,
			forms.idleBrew != nullptr, forms.idleBrewDone != nullptr);

		Log::Info("Forms: glow {}, mark glow {}, sound {}, idles {}/{}/{}, fallback heal {}, time effect {}/{}, NFF roles {}."sv,
			forms.glow != nullptr, forms.markGlow != nullptr, forms.tauntSound != nullptr,
			forms.idleShield != nullptr, forms.idleWeapon != nullptr, forms.idleCry != nullptr,
			forms.fallbackHeal != nullptr, forms.imodIntro != nullptr, forms.imodHold != nullptr,
			forms.nffRole ? "found"sv : "not installed"sv);
	}

	const Set& Get() noexcept
	{
		return forms;
	}
}

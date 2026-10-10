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

	void Resolve()
	{
		const auto& s = Settings::Get();
		forms.glow = FromSpec<RE::TESEffectShader>(s.glowShader, "Taunt glow"sv);
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

		Log::Info("Forms: glow {}, sound {}, idles {}/{}/{}, fallback heal {}, time effect {}/{}, NFF roles {}."sv,
			forms.glow != nullptr, forms.tauntSound != nullptr,
			forms.idleShield != nullptr, forms.idleWeapon != nullptr, forms.idleCry != nullptr,
			forms.fallbackHeal != nullptr, forms.imodIntro != nullptr, forms.imodHold != nullptr,
			forms.nffRole ? "found"sv : "not installed"sv);
	}

	const Set& Get() noexcept
	{
		return forms;
	}
}

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
		// walls), its edge turned a warm white so it reads apart from the red of a taunt's hold
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
		forms.markGlow = Outline(FromSpec<RE::TESEffectShader>(s.markShader, "Mark outline"sv));
		forms.tauntSound = FromSpec<RE::BGSSoundDescriptorForm>(s.tauntSound, "Taunt sound"sv);
		forms.idleShield = FromSpec<RE::TESIdleForm>(s.idleShield, "Shield taunt"sv);
		forms.idleWeapon = FromSpec<RE::TESIdleForm>(s.idleWeapon, "Weapon taunt"sv);
		forms.idleCry = FromSpec<RE::TESIdleForm>(s.idleCry, "War cry"sv);
		forms.imodIntro = FromSpec<RE::TESImageSpaceModifier>(s.imodIntro, "Time effect intro"sv);
		forms.imodHold = FromSpec<RE::TESImageSpaceModifier>(s.imodHold, "Time effect"sv);

		forms.dragon = Lookup<RE::BGSKeyword>("Skyrim.esm"sv, 0x035D59);
		forms.restoreHealth = Lookup<RE::BGSKeyword>("Skyrim.esm"sv, 0x01CEB0);
		forms.boss = Lookup<RE::BGSLocationRefType>("Skyrim.esm"sv, 0x0130F7);
		forms.currentFollower = Lookup<RE::TESFaction>("Skyrim.esm"sv, 0x05C84E);
		forms.defaultStyle = Lookup<RE::TESCombatStyle>("Skyrim.esm"sv, 0x00003D);

		forms.nffRole = Lookup<RE::TESFaction>("nwsFollowerFramework.esp"sv, 0x0094CC);

		forms.idleMagic = Lookup<RE::TESIdleForm>("Skyrim.esm"sv, 0x075CB8);
		forms.handsGreen = Lookup<RE::BGSArtObject>("Skyrim.esm"sv, 0x0ABF11);
		forms.handsBlue = Lookup<RE::BGSArtObject>("Skyrim.esm"sv, 0x0ABF0F);
		forms.healSound = Lookup<RE::BGSSoundDescriptorForm>("Skyrim.esm"sv, 0x03F361);
		forms.rejuvenateSound = Lookup<RE::BGSSoundDescriptorForm>("Skyrim.esm"sv, 0x03D118);

		auto spells = [](auto& a_out, std::initializer_list<RE::FormID> a_ids) {
			std::size_t i = 0;
			for (const auto id : a_ids) {
				a_out[i++] = Lookup<RE::SpellItem>("Skyrim.esm"sv, id);
			}
		};
		spells(forms.flesh, { 0x05AD5C, 0x05AD5D, 0x051B16, 0x05AD5E });
		forms.undead = Lookup<RE::BGSKeyword>("Skyrim.esm"sv, 0x013796);
		spells(forms.bursts[0], { 0x012FD0, 0x01C789, 0x10F7ED });  // Firebolt, Fireball, Incinerate
		spells(forms.bursts[1], { 0x02B96C, 0x045F9C, 0x10F7EC });  // Ice Spike, Ice Storm, Icy Spear
		spells(forms.bursts[2], { 0x02DD29, 0x045F9D, 0x10F7EE });  // Lightning Bolt, Chain Lightning, Thunderbolt
		forms.flameCloak = Lookup<RE::SpellItem>("Skyrim.esm"sv, 0x03AE9F);
		forms.ironArrow = Lookup<RE::TESAmmo>("Skyrim.esm"sv, 0x01397D);
		forms.quakeSound = Lookup<RE::BGSSoundDescriptorForm>("Skyrim.esm"sv, 0x0C4E30);
		forms.cleaveSound = Lookup<RE::BGSSoundDescriptorForm>("Skyrim.esm"sv, 0x0E4CAD);
		forms.chicken = { Lookup<RE::BGSSoundDescriptorForm>("Skyrim.esm"sv, 0x10A00C), Lookup<RE::BGSSoundDescriptorForm>("Skyrim.esm"sv, 0x10A00E) };
		forms.fleshShaders = { Lookup<RE::TESEffectShader>("Skyrim.esm"sv, 0x094160), Lookup<RE::TESEffectShader>("Skyrim.esm"sv, 0x094161),
			Lookup<RE::TESEffectShader>("Skyrim.esm"sv, 0x094163), Lookup<RE::TESEffectShader>("Skyrim.esm"sv, 0x094162) };
		const auto found = [](const auto& a_list) { return std::count_if(a_list.begin(), a_list.end(), [](auto* a_form) { return a_form != nullptr; }); };
		Log::Info("Forms: flesh {}/4."sv, found(forms.flesh));

		Log::Info("Forms: mark glow {}, sound {}, idles {}/{}/{}, casting effects {}/5, time effect {}/{}, NFF roles {}."sv,
			forms.markGlow != nullptr, forms.tauntSound != nullptr,
			forms.idleShield != nullptr, forms.idleWeapon != nullptr, forms.idleCry != nullptr,
			found(std::array<const RE::TESForm*, 5>{ forms.idleMagic, forms.handsGreen, forms.handsBlue, forms.healSound, forms.rejuvenateSound }),
			forms.imodIntro != nullptr, forms.imodHold != nullptr, forms.nffRole ? "found"sv : "not installed"sv);
	}

	const Set& Get() noexcept
	{
		return forms;
	}
}

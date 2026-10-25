#include "FWM/TraitRules.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>

namespace FWM::Traits
{
	namespace
	{
		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.front()))) {
				a_text.remove_prefix(1);
			}
			while (!a_text.empty() && std::isspace(static_cast<unsigned char>(a_text.back()))) {
				a_text.remove_suffix(1);
			}
			return a_text;
		}

		bool SameWord(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		// Takes a_word off the front, with the space or "'s " after it.
		bool DropLeading(std::string_view& a_text, std::string_view a_word)
		{
			if (a_word.empty() || a_text.size() <= a_word.size() || !SameWord(a_text.substr(0, a_word.size()), a_word)) {
				return false;
			}
			auto rest = a_text.substr(a_word.size());
			if (rest.starts_with("'s ")) {
				rest.remove_prefix(3);
			} else if (rest.starts_with(' ')) {
				rest.remove_prefix(1);
			} else {
				return false;
			}
			a_text = Trim(rest);
			return true;
		}

		int Percent(float a_fraction) { return static_cast<int>(std::lround(a_fraction * 100.0f)); }
	}

	bool SignatureSource(std::string_view a_source, std::string_view a_owner, bool a_noted)
	{
		return a_noted || (!a_source.empty() && !a_owner.empty() && SameWord(a_source, a_owner));
	}

	std::size_t Counted(const std::vector<Trait>& a_traits)
	{
		return static_cast<std::size_t>(std::count_if(a_traits.begin(), a_traits.end(), [](const Trait& a_trait) {
			return !a_trait.locked && a_trait.kind != Kind::kNerve;
		}));
	}

	std::string Describe(const Trigger& a_trigger)
	{
		std::string out;
		switch (a_trigger.moment) {
		case Moment::kPowerAttack: out = Tr("On a power attack"); break;
		case Moment::kHit: out = Tr("When their blow lands"); break;
		case Moment::kBash: out = Tr("On a shield bash"); break;
		case Moment::kFightStart: out = Tr("When a fight starts"); break;
		case Moment::kInFight: out = Tr("In a fight"); break;
		default: out = Tr("On a swing"); break;
		}
		if (a_trigger.healthBelow >= 0.0f && a_trigger.healthBelow < 1.0f) {
			out += Trf(" while below {}% health", Percent(a_trigger.healthBelow));
		}
		if (a_trigger.targetHealthAbove > 0.0f) {
			out += Trf(" against a target above {}% health", Percent(a_trigger.targetHealthAbove));
		}
		if (a_trigger.chance > 0 && a_trigger.chance < 100) {
			out += Trf(", {}% of the time", a_trigger.chance);
		}
		return out;
	}

	std::string NerveText(int a_confidence)
	{
		switch (a_confidence) {
		case 0: return std::string(Tr("Runs from danger at the first chance"));
		case 1: return std::string(Tr("Runs once hurt"));
		case 2: return std::string(Tr("Runs when badly hurt"));
		default: return {};
		}
	}

	std::string CleanName(std::string_view a_name, std::string_view a_owner)
	{
		auto name = Trim(a_name);
		const auto owner = Trim(a_owner);
		if (!DropLeading(name, owner)) {
			(void)DropLeading(name, owner.substr(0, owner.find(' ')));  // "Lucien" for "Lucien Flavius"
		}
		static constexpr std::array<std::string_view, 7> filler{ "Spell", "Perk", "Effect", "MGEF", "Apply", "Ability", "FX" };
		for (bool trimmed = true; trimmed;) {
			trimmed = false;
			const auto space = name.find_last_of(' ');
			if (space == std::string_view::npos) {
				break;
			}
			const auto last = name.substr(space + 1);
			for (const auto word : filler) {
				if (SameWord(last, word)) {
					name = Trim(name.substr(0, space));
					trimmed = true;
					break;
				}
			}
		}
		// A name that was nothing but filler says nothing.
		for (const auto word : { std::string_view("Spell"), std::string_view("Perk"), std::string_view("Effect"), std::string_view("MGEF") }) {
			if (SameWord(name, word)) {
				return {};
			}
		}
		return std::string(name);
	}

	std::string PickName(std::string_view a_spell, std::string_view a_perk, std::string_view a_owner)
	{
		auto spell = CleanName(a_spell, a_owner);
		return spell.empty() ? CleanName(a_perk, a_owner) : spell;
	}

	void Merge(std::vector<Trait>& a_traits)
	{
		std::vector<Trait> merged;
		for (auto& trait : a_traits) {
			auto it = std::find_if(merged.begin(), merged.end(), [&](const Trait& a_seen) { return SameWord(a_seen.name, trait.name); });
			if (it == merged.end()) {
				merged.push_back(std::move(trait));
				continue;
			}
			// A merged clause starts lowercase; compare case-insensitively so a third
			// copy does not repeat "or on a power attack" again.
			const auto sameTrigger = std::search(it->when.begin(), it->when.end(), trait.when.begin(), trait.when.end(), [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			});
			if (!trait.when.empty() && sameTrigger == it->when.end()) {
				auto more = trait.when;
				if (!it->when.empty() && !more.empty() && more[0] >= 'A' && more[0] <= 'Z') {
					more[0] = static_cast<char>(more[0] - 'A' + 'a');
				}
				it->when = it->when.empty() ? more : std::format("{}{}{}", it->when, Tr(", or "), more);
			}
			if (it->what.empty()) {
				it->what = std::move(trait.what);
			}
			it->locked = it->locked && trait.locked;
		}
		// Available abilities lead the compact card; locked moves and drawbacks follow.
		std::stable_sort(merged.begin(), merged.end(), [](const Trait& a, const Trait& b) {
			const auto rank = [](const Trait& t) { return t.kind == Kind::kNerve ? 2 : t.locked ? 1 : 0; };
			return rank(a) < rank(b);
		});
		a_traits = std::move(merged);
	}
}

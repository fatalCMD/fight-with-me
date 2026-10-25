#pragma once

#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The words the player reads, in their language. Every line Fight With Me shows, and the Paths
// trees it draws, is written in English in the code and goes through here; the English is also
// the key, so a translation file is the English on the left and the translation on the right.
//
// Files: <Data>/SKSE/Plugins/FightWithMe/Translations/<Mod>_<LANGUAGE>.txt, one entry a line,
// "English<TAB>Translation", in UTF-8 or UTF-16 with a byte order mark. "\n", "\t" and "\\"
// stand for a new line, a tab and a backslash; a line starting with '#' is a comment. A value in
// {} is filled in as the game runs; a translation can put them in another order with {0}, {1}.
// Logs stay English. No game types here: the tests check it.

namespace FWM::Text
{
	// Reads every <anything>_<a_language>.txt in a_folder, replacing what was loaded before. Once,
	// before anything is drawn: the text never changes after, so any thread may read it.
	struct Loaded
	{
		std::vector<std::string> files;
		int                      entries{ 0 };
		int                      unreadable{ 0 };  // lines without a tab, or not UTF-8
	};
	Loaded Load(const std::filesystem::path& a_folder, std::string_view a_language);

	// One file's bytes into the table, for Load and the tests.
	Loaded Add(std::string_view a_bytes);

	// Nothing translated: English again.
	void Clear();

	// The text in the player's language, or a_english itself when nothing translates it. a_english
	// must outlive the result: a literal, or a table entry.
	[[nodiscard]] std::string_view Tr(std::string_view a_english);

	// Marks text kept in a table, translated with Tr where it is shown: the table stays constexpr,
	// and the template script still finds the English.
	[[nodiscard]] constexpr const char* Ntr(const char* a_english) { return a_english; }

	// Tr for an API that wants a C string. Only for a literal, which ends in a nul as every
	// translation does.
	[[nodiscard]] inline const char* Trc(const char* a_english) { return Tr(a_english).data(); }

	// A translation of a sentence with values in it, or nothing when there is none or it does not
	// fit the values (a {} too many, say): the English is used then.
	[[nodiscard]] std::optional<std::string> Format(std::string_view a_english, std::format_args a_args);

	// A sentence with values in it, in the player's language.
	template <class... Args>
	[[nodiscard]] std::string Trf(std::format_string<Args...> a_english, Args&&... a_args)
	{
		if (auto text = Format(a_english.get(), std::make_format_args(a_args...))) {
			return std::move(*text);
		}
		return std::format(a_english, std::forward<Args>(a_args)...);
	}

	// A count and its noun: a_one for exactly one, a_other for the rest, each with {} for the count.
	[[nodiscard]] std::string Trn(long long a_count, std::format_string<long long> a_one, std::format_string<long long> a_other);

	// --- UTF-8 ------------------------------------------------------------------------------

	// The byte length of the character starting at a_at (1 for a broken one), so text can be
	// walked a whole character at a time.
	[[nodiscard]] std::size_t CharLength(std::string_view a_text, std::size_t a_at);

	// Capitals, in any script the system knows: "é" becomes "É", "д" becomes "Д".
	[[nodiscard]] std::string Upper(std::string_view a_text);

	// The tab-separated line a template has for a_english: escapes written out, the English on both sides.
	[[nodiscard]] std::string Escape(std::string_view a_text);
}

namespace FWM
{
	using Text::Ntr;
	using Text::Tr;
	using Text::Trc;
	using Text::Trf;
	using Text::Trn;
}

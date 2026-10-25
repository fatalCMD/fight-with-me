#include "FWM/Text.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace FWM::Text
{
	namespace
	{
		struct Hash
		{
			using is_transparent = void;
			std::size_t operator()(std::string_view a_text) const noexcept { return std::hash<std::string_view>{}(a_text); }
		};

		std::unordered_map<std::string, std::string, Hash, std::equal_to<>> table;

		std::wstring Wide(std::string_view a_text, bool a_strict)
		{
			if (a_text.empty()) {
				return {};
			}
			const auto flags = a_strict ? MB_ERR_INVALID_CHARS : 0;
			const int  size = ::MultiByteToWideChar(CP_UTF8, flags, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
			std::wstring out(static_cast<std::size_t>(std::max(size, 0)), L'\0');
			if (size > 0) {
				::MultiByteToWideChar(CP_UTF8, flags, a_text.data(), static_cast<int>(a_text.size()), out.data(), size);
			}
			return out;
		}

		std::string Narrow(std::wstring_view a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const int   size = ::WideCharToMultiByte(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
			if (size > 0) {
				::WideCharToMultiByte(CP_UTF8, 0, a_text.data(), static_cast<int>(a_text.size()), out.data(), size, nullptr, nullptr);
			}
			return out;
		}

		// A file's bytes as UTF-8, whichever of the usual encodings it was saved in.
		std::string AsUtf8(std::string_view a_bytes)
		{
			const auto at = [&](std::size_t i) { return static_cast<unsigned char>(a_bytes[i]); };
			if (a_bytes.size() >= 2 && ((at(0) == 0xFF && at(1) == 0xFE) || (at(0) == 0xFE && at(1) == 0xFF))) {
				const bool          little = at(0) == 0xFF;
				std::wstring        wide;
				for (std::size_t i = 2; i + 1 < a_bytes.size(); i += 2) {
					wide.push_back(static_cast<wchar_t>(little ? at(i) | at(i + 1) << 8 : at(i) << 8 | at(i + 1)));
				}
				return Narrow(wide);
			}
			if (a_bytes.size() >= 3 && at(0) == 0xEF && at(1) == 0xBB && at(2) == 0xBF) {
				a_bytes.remove_prefix(3);
			}
			return std::string(a_bytes);
		}

		std::string Unescape(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size());
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				if (a_text[i] == '\\' && i + 1 < a_text.size()) {
					switch (a_text[i + 1]) {
					case 'n':
						out.push_back('\n');
						++i;
						continue;
					case 't':
						out.push_back('\t');
						++i;
						continue;
					case '\\':
						out.push_back('\\');
						++i;
						continue;
					default:
						break;
					}
				}
				out.push_back(a_text[i]);
			}
			return out;
		}

		bool EndsWithNoCase(std::string_view a_text, std::string_view a_end)
		{
			return a_text.size() >= a_end.size() && std::equal(a_end.begin(), a_end.end(), a_text.end() - static_cast<std::ptrdiff_t>(a_end.size()), [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			});
		}
	}

	Loaded Add(std::string_view a_bytes)
	{
		Loaded     loaded;
		const auto text = AsUtf8(a_bytes);
		std::size_t start = 0;
		while (start <= text.size()) {
			auto end = text.find('\n', start);
			if (end == std::string::npos) {
				end = text.size();
			}
			std::string_view line(text.data() + start, end - start);
			start = end + 1;
			if (!line.empty() && line.back() == '\r') {
				line.remove_suffix(1);
			}
			if (line.empty() || line.front() == '#') {
				if (end == text.size()) {
					break;
				}
				continue;
			}
			const auto tab = line.find('\t');
			if (tab == std::string_view::npos || (Wide(line, true).empty() && !line.empty())) {
				++loaded.unreadable;
			} else {
				auto english = Unescape(line.substr(0, tab));
				auto translated = Unescape(line.substr(tab + 1));
				// An empty right side leaves the English, so a half-done file still reads.
				if (!english.empty() && !translated.empty()) {
					table.insert_or_assign(std::move(english), std::move(translated));
					++loaded.entries;
				}
			}
			if (end == text.size()) {
				break;
			}
		}
		return loaded;
	}

	Loaded Load(const std::filesystem::path& a_folder, std::string_view a_language)
	{
		Clear();
		Loaded          all;
		std::error_code ec;
		if (a_language.empty() || !std::filesystem::is_directory(a_folder, ec)) {
			return all;
		}
		const auto                         suffix = std::format("_{}.txt", a_language);
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(a_folder, ec)) {
			const auto name = entry.path().filename().string();
			if (entry.is_regular_file(ec) && EndsWithNoCase(name, suffix)) {
				files.push_back(entry.path());
			}
		}
		std::sort(files.begin(), files.end());
		for (const auto& path : files) {
			std::ifstream      in(path, std::ios::binary);
			const std::string  bytes{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
			const auto         one = Add(bytes);
			all.entries += one.entries;
			all.unreadable += one.unreadable;
			all.files.push_back(path.filename().string());
		}
		return all;
	}

	void Clear()
	{
		table.clear();
	}

	std::string_view Tr(std::string_view a_english)
	{
		if (table.empty()) {
			return a_english;
		}
		const auto it = table.find(a_english);
		return it != table.end() ? std::string_view(it->second) : a_english;
	}

	std::optional<std::string> Format(std::string_view a_english, std::format_args a_args)
	{
		const auto text = Tr(a_english);
		if (text.data() == a_english.data()) {
			return std::nullopt;
		}
		try {
			return std::vformat(text, a_args);
		} catch (const std::format_error&) {
			return std::nullopt;
		}
	}

	std::string Trn(long long a_count, std::format_string<long long> a_one, std::format_string<long long> a_other)
	{
		const auto& chosen = a_count == 1 ? a_one : a_other;
		if (auto text = Format(chosen.get(), std::make_format_args(a_count))) {
			return std::move(*text);
		}
		return std::vformat(chosen.get(), std::make_format_args(a_count));
	}

	std::size_t CharLength(std::string_view a_text, std::size_t a_at)
	{
		if (a_at >= a_text.size()) {
			return 0;
		}
		const auto  lead = static_cast<unsigned char>(a_text[a_at]);
		std::size_t length = lead < 0x80 ? 1 : (lead >= 0xF0 && lead < 0xF8 ? 4 : (lead >= 0xE0 ? 3 : (lead >= 0xC0 ? 2 : 1)));
		length = std::min(length, a_text.size() - a_at);
		for (std::size_t i = 1; i < length; ++i) {
			if ((static_cast<unsigned char>(a_text[a_at + i]) & 0xC0) != 0x80) {
				return 1;  // broken: one byte at a time, so nothing is skipped
			}
		}
		return length;
	}

	std::string Upper(std::string_view a_text)
	{
		const auto wide = Wide(a_text, false);
		if (wide.empty()) {
			return std::string(a_text);
		}
		std::wstring out(wide.size() * 2 + 1, L'\0');
		const int    size = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wide.data(), static_cast<int>(wide.size()), out.data(), static_cast<int>(out.size()),
			   nullptr, nullptr, 0);
		if (size <= 0) {
			return std::string(a_text);
		}
		out.resize(static_cast<std::size_t>(size));
		return Narrow(out);
	}

	std::string Escape(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size());
		for (const char c : a_text) {
			switch (c) {
			case '\n':
				out += "\\n";
				break;
			case '\t':
				out += "\\t";
				break;
			case '\\':
				out += "\\\\";
				break;
			default:
				out.push_back(c);
			}
		}
		return out;
	}
}

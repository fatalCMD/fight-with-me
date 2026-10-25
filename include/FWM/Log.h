#pragma once

#include <filesystem>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

// A wide-path ofstream rather than spdlog, so a Documents folder named in a
// non-Latin script cannot stop the log opening.

namespace FWM::Log
{
	// <My Games>/Skyrim Special Edition/SKSE, where SKSE itself logs.
	[[nodiscard]] std::optional<std::filesystem::path> Directory();

	// Opens <name>.log, keeping the previous run as <name>.previous.log.
	void Setup(std::wstring_view a_name = L"FightWithMe");

	void Write(std::string_view a_level, std::string_view a_message);

	template <class... Args>
	void WriteFormatted(std::string_view a_level, std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		try {
			Write(a_level, std::format(a_fmt, std::forward<Args>(a_args)...));
		} catch (const std::format_error&) {
			// Compile-time format checks cannot validate runtime values (for example,
			// a native null string or a negative width). Keep diagnostics out of the
			// game's exception path and retain the template without formatting again.
			Write("warn", "[Log] Could not format message; original template follows.");
			Write(a_level, a_fmt.get());
		}
	}

	template <class... Args>
	void Info(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		WriteFormatted("info", a_fmt, std::forward<Args>(a_args)...);
	}

	template <class... Args>
	void Warn(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		WriteFormatted("warn", a_fmt, std::forward<Args>(a_args)...);
	}

	template <class... Args>
	void Error(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		WriteFormatted("error", a_fmt, std::forward<Args>(a_args)...);
	}
}

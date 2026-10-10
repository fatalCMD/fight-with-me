#pragma once

// A wide-path ofstream rather than spdlog, so a Documents folder named in a
// non-Latin script cannot stop the log opening.

namespace FWM::Log
{
	// <My Games>/Skyrim Special Edition/SKSE, where SKSE itself logs.
	[[nodiscard]] std::optional<std::filesystem::path> Directory();

	// Opens FightWithMe.log, keeping the previous run as FightWithMe.previous.log.
	void Setup();

	void Write(std::string_view a_level, std::string_view a_message);

	template <class... Args>
	void Info(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		Write("info"sv, std::format(a_fmt, std::forward<Args>(a_args)...));
	}

	template <class... Args>
	void Warn(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		Write("warn"sv, std::format(a_fmt, std::forward<Args>(a_args)...));
	}

	template <class... Args>
	void Error(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		Write("error"sv, std::format(a_fmt, std::forward<Args>(a_args)...));
	}
}

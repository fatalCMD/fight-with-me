#include "FWM/Log.h"

namespace FWM::Log
{
	namespace
	{
		std::mutex    lock;
		std::ofstream file;
	}

	std::optional<std::filesystem::path> Directory()
	{
		try {
			return SKSE::log::log_directory();
		} catch (...) {
			return std::nullopt;
		}
	}

	void Setup()
	{
		const std::scoped_lock guard(lock);
		try {
			const auto directory = Directory();
			if (!directory) {
				return;
			}
			std::error_code ec;
			std::filesystem::create_directories(*directory, ec);

			const auto current = *directory / L"FightWithMe.log";
			if (std::filesystem::exists(current, ec)) {
				std::filesystem::rename(current, *directory / L"FightWithMe.previous.log", ec);
			}
			file.open(current, std::ios::out | std::ios::trunc);
		} catch (...) {
			// A logger must never take down the thing it reports on.
		}
	}

	void Write(std::string_view a_level, std::string_view a_message)
	{
		const std::scoped_lock guard(lock);
		if (!file.is_open()) {
			return;
		}
		SYSTEMTIME now{};
		::GetLocalTime(&now);
		file << std::format("[{:02}:{:02}:{:02}.{:03}] [{}] {}\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, a_level, a_message);
		file.flush();
	}
}

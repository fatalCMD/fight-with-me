#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::literals;
#include "FWM/Log.h"

namespace
{
	std::vector<std::pair<std::string, std::string>> messages;
	int failures = 0;

	void Check(bool a_ok, const char* a_name)
	{
		if (!a_ok) {
			++failures;
			std::printf("FAIL: %s\n", a_name);
		}
	}

	template <class Emit>
	void CheckMessage(const char* a_name, Emit a_emit, std::string_view a_level, std::string_view a_expected)
	{
		messages.clear();
		try {
			a_emit();
			Check(!messages.empty() && messages.back().first == a_level && messages.back().second == a_expected, a_name);
		} catch (const std::exception& error) {
			++failures;
			std::printf("FAIL: %s threw: %s\n", a_name, error.what());
		}
	}
}

// Capture the real header's formatted messages instead of opening a game log.
namespace FWM::Log
{
	void Write(std::string_view a_level, std::string_view a_message)
	{
		messages.emplace_back(a_level, a_message);
	}
}

int main()
{
	using namespace FWM;
	CheckMessage("ground burst model and duration", [] {
		Log::Info("[Effects] Ground burst animation ready: {} ({:.1f} s).", std::string{ R"(magic\EarthRockExplosion01.nif)" }, 6.6667f);
	}, "info", R"([Effects] Ground burst animation ready: magic\EarthRockExplosion01.nif (6.7 s).)");

	const char* missingModel = nullptr;
	// This exact call threw 'String pointer is null.' in MSVC's formatter in-game.
	CheckMessage("null model in info does not escape", [&] {
		Log::Info("[Effects] Ground burst animation ready: {} ({:.1f} s).", missingModel, 6.6667f);
	}, "info", "[Effects] Ground burst animation ready: {} ({:.1f} s).");
	CheckMessage("null model in warning does not escape", [&] {
		Log::Warn("[Effects] Ground burst loaded without SpecialIdle_AreaEffect: {}.", missingModel);
	}, "warn", "[Effects] Ground burst loaded without SpecialIdle_AreaEffect: {}.");
	CheckMessage("null argument in error does not escape", [&] {
		Log::Error("Missing model: {}.", missingModel);
	}, "error", "Missing model: {}.");
	CheckMessage("invalid runtime width does not escape", [] {
		Log::Warn("Value: {:{}}.", 5, -1);
	}, "warn", "Value: {:{}}.");
	CheckMessage("empty string remains valid", [] {
		Log::Info("Name: '{}'.", "");
	}, "info", "Name: ''.");
	CheckMessage("logging works after a formatting failure", [] {
		Log::Info("Next burst: {}.", 2);
	}, "info", "Next burst: 2.");
	std::printf("Log regression checks: %d failure(s).\n", failures);
	return failures == 0 ? 0 : 1;
}

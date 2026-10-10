#pragma once

// Short lines shown on the HUD ("Lydia taunts: three enemies turn on her").
// Pushed from the main thread, read by the menu's render callback.

namespace FWM::Notices
{
	enum class Kind : std::uint8_t
	{
		kInfo,
		kTaunt,
		kHeal,
		kWarning,
		kOrder
	};

	struct Notice
	{
		std::string   text;
		Kind          kind{ Kind::kInfo };
		std::uint64_t at{ 0 };  // GetTickCount64 when pushed
	};

	void Push(std::string a_text, Kind a_kind = Kind::kInfo);

	// Notices younger than a_maxAgeMs, oldest first.
	[[nodiscard]] std::vector<Notice> Recent(std::uint64_t a_maxAgeMs);
}

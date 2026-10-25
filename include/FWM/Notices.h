#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace FWM::Paths { enum class Ability : std::uint8_t; }

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
		std::uint32_t icon{ 0 };  // the same glyph or image used in the perk tree
		std::string   title;
	};

	void Push(std::string_view a_text, Kind a_kind = Kind::kInfo, std::uint32_t a_icon = 0, std::string_view a_title = {});
	void Used(std::string_view a_text, Paths::Ability a_ability, Kind a_kind = Kind::kInfo);

	// Notices younger than a_maxAgeMs, oldest first.
	[[nodiscard]] std::vector<Notice> Recent(std::uint64_t a_maxAgeMs);
}

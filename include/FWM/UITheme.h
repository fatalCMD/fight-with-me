#pragma once

#include <cstdint>

#include "FWM/Text.h"

// Shared by the battle plan, Paths, HUD notices and the settings pages.
// Resource and warning colours belong to their meaning, not to the chosen theme.
namespace FWM::UITheme
{
	constexpr std::uint32_t RGBA(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a = 255)
	{
		return r | (g << 8) | (b << 16) | (a << 24);
	}

	enum class Style { Vanilla, Oxblood, Nordic };
	inline constexpr int kCount = 3;
	constexpr int Normalize(int a_theme) { return a_theme >= 0 && a_theme < kCount ? a_theme : 0; }
	constexpr const char* Name(int a_theme)
	{
		switch (Normalize(a_theme)) {
		case 1: return Ntr("Oxblood");
		case 2: return Ntr("Nordic");
		default: return Ntr("Vanilla");
		}
	}

	struct Palette
	{
		Style style;
		std::uint32_t glass, glassEdge, tile, tileHover, accent, accentDim, edge;
		std::uint32_t text, textDim, textFaint, focusRing;
		std::uint32_t inset, points, vignette;
		std::uint32_t orbShadow, orbDormant, orbRim, orbHalo, orbEdge, orbHeart, orbBand, orbMote, orbSpark;
		std::uint32_t frameHover, frameActive, buttonHover, buttonActive, sliderActive, header, headerHover;
		constexpr bool Subdued() const { return style != Style::Oxblood; }
		constexpr bool Engraved() const { return style == Style::Nordic; }
	};

	inline constexpr Palette Vanilla{
		Style::Vanilla,
		RGBA(11, 11, 11, 218), RGBA(213, 213, 213, 88), RGBA(238, 238, 238, 7), RGBA(238, 238, 238, 22),
		RGBA(82, 82, 82), RGBA(169, 169, 169), RGBA(219, 219, 219),
		RGBA(235, 235, 235), RGBA(184, 184, 184), RGBA(132, 132, 132), RGBA(240, 240, 240),
		RGBA(24, 24, 24, 245), RGBA(45, 45, 45, 250), RGBA(10, 10, 10, 65),
		RGBA(22, 22, 22, 245), RGBA(63, 63, 63), RGBA(183, 183, 183), RGBA(189, 189, 189),
		RGBA(113, 113, 113), RGBA(246, 246, 246), RGBA(215, 215, 215), RGBA(219, 219, 219), RGBA(248, 248, 248),
		RGBA(57, 57, 57, 232), RGBA(78, 78, 78, 240), RGBA(67, 67, 67, 236), RGBA(91, 91, 91, 240),
		RGBA(243, 243, 243), RGBA(53, 53, 53, 230), RGBA(75, 75, 75, 236)
	};

	inline constexpr Palette Oxblood{
		Style::Oxblood,
		RGBA(15, 15, 16, 236), RGBA(255, 255, 255, 54), RGBA(255, 255, 255, 8), RGBA(255, 255, 255, 20),
		RGBA(158, 36, 34), RGBA(150, 146, 140), RGBA(212, 66, 58),
		RGBA(236, 232, 226), RGBA(170, 166, 160), RGBA(108, 106, 102), RGBA(236, 232, 226),
		RGBA(22, 12, 12, 245), RGBA(58, 15, 14, 250), RGBA(10, 8, 8, 80),
		RGBA(20, 18, 22, 245), RGBA(60, 46, 44), RGBA(200, 150, 120), RGBA(255, 150, 70),
		RGBA(150, 32, 24), RGBA(255, 246, 222), RGBA(255, 214, 160), RGBA(255, 210, 150), RGBA(255, 248, 230),
		RGBA(72, 24, 22, 232), RGBA(98, 28, 26, 240), RGBA(86, 24, 22, 236), RGBA(122, 32, 30, 240),
		RGBA(238, 100, 88), RGBA(76, 22, 20, 230), RGBA(100, 28, 26, 236)
	};

	inline constexpr Palette Nordic{
		Style::Nordic,
		RGBA(15, 21, 22, 242), RGBA(155, 166, 160, 148), RGBA(225, 226, 209, 10), RGBA(225, 226, 209, 28),
		RGBA(66, 76, 72), RGBA(166, 172, 156), RGBA(207, 214, 195),
		RGBA(236, 235, 221), RGBA(183, 191, 179), RGBA(128, 140, 134), RGBA(245, 244, 227),
		RGBA(21, 29, 30, 250), RGBA(47, 61, 58, 250), RGBA(8, 12, 13, 65),
		RGBA(16, 25, 29, 245), RGBA(46, 72, 80), RGBA(169, 207, 211), RGBA(170, 215, 217),
		RGBA(66, 116, 127), RGBA(245, 250, 237), RGBA(194, 220, 209), RGBA(194, 225, 219), RGBA(246, 250, 238),
		RGBA(47, 61, 58, 240), RGBA(65, 80, 73, 245), RGBA(54, 69, 64, 240), RGBA(76, 91, 81, 245),
		RGBA(239, 242, 221), RGBA(40, 54, 51, 235), RGBA(58, 73, 66, 240)
	};

	// INI: 0 = Vanilla, 1 = Oxblood, 2 = Nordic. Unknown values safely use Vanilla.
	constexpr const Palette& For(int a_theme)
	{
		switch (Normalize(a_theme)) {
		case 1: return Oxblood;
		case 2: return Nordic;
		default: return Vanilla;
		}
	}
}

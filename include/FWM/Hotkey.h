#pragma once

#include <cstdint>

// Keyboard bindings: a DirectInput scan code, optionally with Shift, Ctrl or Alt held.
// No game types here, so the tests and the menu preview can use it.

namespace FWM
{
	struct Hotkey
	{
		std::uint32_t key{ 0 };       // 0 = unbound
		std::uint32_t modifier{ 0 };  // the left-hand scan code of Shift, Ctrl or Alt; 0 = none

		friend bool operator==(const Hotkey&, const Hotkey&) = default;
	};

	inline constexpr std::uint32_t kShift = 0x2A;
	inline constexpr std::uint32_t kCtrl = 0x1D;
	inline constexpr std::uint32_t kAlt = 0x38;

	// Either side of Shift, Ctrl or Alt as the left-hand key; 0 for any other key.
	[[nodiscard]] constexpr std::uint32_t ModifierOf(std::uint32_t a_key) noexcept
	{
		switch (a_key) {
		case 0x2A:
		case 0x36:
			return kShift;
		case 0x1D:
		case 0x9D:
			return kCtrl;
		case 0x38:
		case 0xB8:
			return kAlt;
		default:
			return 0;
		}
	}

	// A binding as it can be pressed: a modifier only with a key, never a modifier on itself.
	[[nodiscard]] constexpr Hotkey Normalized(Hotkey a_hotkey) noexcept
	{
		a_hotkey.modifier = ModifierOf(a_hotkey.modifier);
		if (a_hotkey.key == 0 || ModifierOf(a_hotkey.key) != 0) {
			a_hotkey.modifier = 0;
		}
		return a_hotkey;
	}

	// How well a key press fits a binding: 2 with its modifier held, 1 for a binding that
	// needs none (held modifiers do not stop it: Alt is sprint), 0 not at all. The best
	// fit wins, so Ctrl + Numpad 8 and Numpad 8 can be bound to different things.
	[[nodiscard]] constexpr int Fit(const Hotkey& a_hotkey, std::uint32_t a_key, bool a_modifierHeld) noexcept
	{
		if (a_hotkey.key == 0 || a_hotkey.key != a_key) {
			return 0;
		}
		if (a_hotkey.modifier == 0) {
			return 1;
		}
		return a_modifierHeld ? 2 : 0;
	}
}

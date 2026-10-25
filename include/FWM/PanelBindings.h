#pragma once
#include "FWM/Hotkey.h"

namespace FWM::PanelBindings {
// Only repair untouched pre-split defaults that collide with the battle plan.
// Explicit user bindings, modified chords, and standalone setups stay intact.
constexpr Hotkey Keyboard(Hotkey paths, Hotkey orders, bool paired, bool userBinding) {
    return paired && !userBinding && paths == Hotkey{71,0} && paths == orders ? Hotkey{73,0} : paths;
}
constexpr std::uint32_t Gamepad(std::uint32_t paths, std::uint32_t orders, bool paired, bool userBinding) {
    return paired && !userBinding && paths == 2 && paths == orders ? 8 : paths;
}
}

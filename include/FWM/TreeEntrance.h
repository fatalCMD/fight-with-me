#pragma once
#include <algorithm>
namespace FWM::TreeEntrance {
constexpr float kReveal = .78f;
constexpr float kComplete = 1.25f;
constexpr float Ease(float t) { t=std::clamp(t,0.f,1.f);return t*t*(3.f-2.f*t); }
struct Frame { float lift, black; bool visible, interactive; };
constexpr Frame At(float seconds) {
 return {Ease(seconds/.65f),seconds<kReveal?Ease((seconds-.12f)/.53f):1.f-Ease((seconds-kReveal)/(kComplete-kReveal)),seconds>=kReveal,seconds>=kComplete};
}
constexpr float kExitSwap=.32f, kExitReveal=.65f, kExitComplete=1.15f;
constexpr float kSwitchSwap=.42f, kSwitchReveal=.54f, kSwitchComplete=1.02f;
constexpr float ExitBlack(float seconds) {
 return seconds<kExitReveal?Ease(seconds/kExitSwap):1.f-Ease((seconds-kExitReveal)/(kExitComplete-kExitReveal));
}
// Horizontal coverage: cover from the left, hold black, then uncover to the right.
struct Wipe { float left, right; };
constexpr Wipe SwitchWipe(float seconds) {
 return {Ease((seconds-kSwitchReveal)/(kSwitchComplete-kSwitchReveal)),Ease(seconds/kSwitchSwap)};
}
}

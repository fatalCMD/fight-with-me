#pragma once
#include <array>
#include <cstddef>
#include <string>

namespace FWM::Berserk {
inline constexpr std::size_t kCopies=4, kPlumes=12, kEmbers=48, kParts=2+kPlumes+kEmbers;
inline constexpr float kFade=1.1f, kImpact=.24f;
struct Part {
    std::array<float,3> at{}, color{1,.24f,.035f};
    float scale{1}, turn{}, tilt{}, alpha{}, drift{};
};
struct Frame { std::array<Part,kParts> parts; float light{}; };
std::string Name(std::size_t index);
// The duration is the real buff deadline, including Bloodlust extensions.
Frame At(float age,float duration,int rank);
}

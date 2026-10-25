#pragma once
#include <array>
#include <cstddef>
#include <string>
namespace FWM::SecondWindFx {
inline constexpr std::size_t kCopies=8, kWisps=12, kMotes=56, kParts=3+kWisps+kMotes;
inline constexpr float kSeconds=2.6f;
struct Part {
    std::array<float,3> at{}, color{.18f,1,.57f};
    float scale{1}, turn{}, tilt{}, alpha{}, drift{};
};
struct Frame { std::array<Part,kParts> parts; float light{}; };
std::string Name(std::size_t index);
Frame At(float age,int rank);
}

#pragma once
#include <array>
#include <cstddef>
namespace FWM::DisarmFx {
inline constexpr std::size_t kSparks=32, kParts=kSparks+3, kCopies=16;
inline constexpr float kSeconds=0.85f;
struct Part { std::array<float,3> at{}; float scale{}, alpha{}; std::array<float,3> color{1,1,1}; };
struct Look { std::array<Part,kParts> parts{}; float light{}; };
[[nodiscard]] Look At(float age,int rank);
}

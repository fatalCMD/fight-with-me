#pragma once
#include <array>
#include <span>
namespace FWM::SkaldMusic {
inline constexpr std::array beats0{0.197f,0.632f,1.067f,1.501f,1.936f,2.371f,2.806f,3.240f,3.675f,4.110f,4.545f,4.980f,5.414f,5.849f,6.284f,6.719f,7.154f,7.588f,8.023f,8.458f};
inline constexpr std::array beats1{0.496f,0.972f,1.448f,1.925f,2.401f,2.877f,3.353f,3.829f,4.306f,4.782f,5.258f,5.734f,6.210f,6.686f,7.163f,7.639f,8.115f,8.591f,9.067f,9.544f};
inline constexpr std::array beats2{0.101f,0.577f,1.053f,1.530f,2.006f,2.482f,2.958f,3.434f,3.911f,4.387f,4.863f,5.339f,5.815f,6.291f,6.768f,7.244f,7.720f,8.196f,8.672f,9.149f};
inline constexpr std::array beats3{0.528f,0.983f,1.437f,1.892f,2.346f,2.801f,3.255f,3.710f,4.164f,4.619f,5.073f,5.528f,5.983f,6.437f,6.892f,7.346f,7.801f,8.255f,8.710f,9.164f};
inline constexpr std::array waves0{0.197f,1.067f,1.936f,2.806f,3.675f,4.545f,5.414f,6.284f,7.154f,8.023f};
inline constexpr std::array waves1{0.496f,1.448f,2.401f,3.353f,4.306f,5.258f,6.210f,7.163f,8.115f,9.067f};
inline constexpr std::array waves2{0.101f,1.053f,2.006f,2.958f,3.911f,4.863f,5.815f,6.768f,7.720f,8.672f};
inline constexpr std::array waves3{0.528f,1.437f,2.346f,3.255f,4.164f,5.073f,5.983f,6.892f,7.801f,8.710f};
struct Track {const char* file; float seconds; float bpm; std::span<const float> beats; std::span<const float> waves;};
inline constexpr std::array<Track,4> tracks{{
 {R"(Sound\fx\FightWithMe\Skald\SkaldFervor.wav)",8.893f,138.0f,beats0,waves0},
 {R"(Sound\fx\FightWithMe\Skald\SkaldEchoes.wav)",10.02f,126.0f,beats1,waves1},
 {R"(Sound\fx\FightWithMe\Skald\SkaldHollow.wav)",9.625f,126.0f,beats2,waves2},
 {R"(Sound\fx\FightWithMe\Skald\SkaldGlory.wav)",9.619f,132.0f,beats3,waves3},
}};
}

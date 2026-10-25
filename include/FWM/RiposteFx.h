#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace FWM::RiposteFx {
inline constexpr std::size_t kParts=43, kCopies=16;
inline constexpr float kSeconds=1.05f, kCounterDelay=.22f;
constexpr int Rank(int r){return std::clamp(r,1,3);}
constexpr float DownSeconds(int r){return .5f+static_cast<float>(Rank(r));}
constexpr float ImpactMultiplier(int r){return .5f*static_cast<float>(Rank(r));}
constexpr float PushSpeed(int r){return 180.0f+40.0f*static_cast<float>(Rank(r));}
// A fall only counts after ragdoll activation, near a floor, with the pelvis settling.
inline bool Landed(float age,float floorGap,float verticalSpeed,float settled){
 return std::isfinite(floorGap)&&std::isfinite(verticalSpeed)&&age>=.3f&&floorGap>=-5&&floorGap<45&&std::abs(verticalSpeed)<45&&settled>=.10f;
}
struct Part {std::array<float,3> at{};float scale{},alpha{};std::array<float,3> color{1,1,1};};
struct Look {std::array<Part,kParts> parts{};float light{};};
[[nodiscard]] Look At(float age,int rank,bool ground);
}

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
namespace FWM::WindstepFx {
inline constexpr float kTravel=.65f, kFade=.9f, kVisual=6.9f;
inline constexpr std::size_t kParts=52, kCopies=8;
inline int Rank(int r){return std::clamp(r,1,3);}
inline float Reach(int r){return 175.f+35.f*static_cast<float>(Rank(r));}
inline float Duration(int r){return 1.f+static_cast<float>(Rank(r));}
inline float EchoHold(int r){return 3.f+static_cast<float>(Rank(r));}
inline float VisualSeconds(int r){return EchoHold(r)+kFade;}
inline float Protection(int r){return Rank(r)==1?0.f:Rank(r)==2?.2f:.3f;}
inline float Ease(float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);}
inline float Travel(float t){return Ease(t/kTravel);}
inline float Speed(float t,float reach){const float u=std::clamp(t/kTravel,0.f,1.f);return reach*6*u*(1-u)/kTravel;}
// Fail closed at ledges, steep steps, or inadequate collision clearance.
inline bool SafeSample(bool ground,float heightChange,float obstacleFraction){return ground&&std::isfinite(heightChange)&&std::abs(heightChange)<=24&&obstacleFraction>=.999f;}
struct Part {std::array<float,3> at{},color{.48f,.9f,1};float scale{1},alpha{},turn{},drift{};};
struct Frame {std::array<Part,kParts> parts{};};
inline std::string Name(std::size_t i){return "Part"+(i<10?std::string("0"):std::string{})+std::to_string(i);}
Frame At(float age,int rank);
}

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
namespace FWM::MoonFx {
inline constexpr std::size_t kCopies=24, kWisps=12, kMotes=48, kWraps=48, kParts=6+kWisps+kMotes+kWraps+1;
inline constexpr float kApparition=7.2f, kSeconds=17, kRelease=1.4f, kTravel=1;
inline float Chance(int rank){return .15f+.05f*static_cast<float>(std::clamp(rank,1,3));}
inline float Duration(int rank){return 8+2*static_cast<float>(std::clamp(rank,1,3));}
inline float Radius(int rank){return 700+140*static_cast<float>(std::clamp(rank,1,3));}
inline bool Negates(float chance,float left,bool hostile,float damage,float roll){
 return left>0 && hostile && std::isfinite(damage) && damage>0 && roll>=0 && roll<std::clamp(chance,0.0f,.75f);
}
inline float Wave(float age,float radius){return std::clamp((age-kRelease)/kTravel,0.0f,1.0f)*radius;}
struct Part {std::array<float,3> at{},color{};float scale{1},turn{},tilt{},alpha{},drift{};};
struct Frame {std::array<Part,kParts> parts;float light{};};
std::string Name(std::size_t index);
Frame Aura(float age,float left,int rank);
Frame At(float age,int rank,float radius=0);
}

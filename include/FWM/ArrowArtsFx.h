#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace FWM::ArrowArtsFx {
inline constexpr std::size_t kParts=40,kCopies=24;
inline constexpr float kCharge=.65f,kPrimeSeconds=12.f,kFlightLimit=8.f,kFlash=.65f,kWindSeconds=.32f;
constexpr int Rank(int r){return std::clamp(r,1,3);}
constexpr float Damage(int r){return std::array{24.f,36.f,50.f}[static_cast<std::size_t>(Rank(r)-1)];}
constexpr float Radius(int r){return 175.f+35.f*static_cast<float>(Rank(r));}
inline float Hurt(float damage,float distance,float radius,float resistance){
 if(!std::isfinite(distance)||!std::isfinite(radius)||!std::isfinite(resistance)||!std::isfinite(damage)||distance<0||radius<=0||distance>radius)return 0;
 return std::max(0.f,damage)*(1-.35f*std::clamp(distance/radius,0.f,1.f))*(1-std::clamp(resistance,0.f,85.f)/100);
}
// The buff cannot attach retroactively to an arrow that was already in flight.
struct PrimeWindow {
 float age{};bool spent{};
 void Advance(float dt){if(std::isfinite(dt)&&dt>0)age+=dt;}
 bool Expired() const {return age>=kPrimeSeconds;}
 bool Claim(float projectileAge){
  if(spent||Expired()||!std::isfinite(projectileAge)||projectileAge<0||projectileAge>age+.001f)return false;
  spent=true;return true;
 }
};
struct ImpactGate {bool spent{};bool Resolve(bool collision){if(spent||!collision)return false;spent=true;return true;}};
enum class Kind {Wind,Rune};
struct Part {std::array<float,3> at{};float scale{},alpha{},turn{};std::array<float,3> color{1,1,1};};
struct Look {std::array<Part,kParts> parts{};float light{};};
[[nodiscard]] Look At(Kind kind,float age,int rank,float impact=-1);
}

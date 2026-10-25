#pragma once
#include "FWM/Orders.h"
#include <algorithm>
#include <cmath>
namespace FWM::AbilityResource {
constexpr float Share(int rank) { return .05f + .05f * static_cast<float>(std::clamp(rank,1,3)); }
inline float Cost(float maximum,int rank) { return std::isfinite(maximum) && maximum > 0 ? maximum*Share(rank) : 0; }
inline bool Enough(float current,float maximum,int rank) { return std::isfinite(current) && std::isfinite(maximum) && maximum > 0 && current >= Cost(maximum,rank); }
}

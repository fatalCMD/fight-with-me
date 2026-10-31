#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace FWM::TargetPolicy {
// Empty onlyFrom preserves ordinary orders and taunts. A protected-target list
// redirects only those native choices, never a valid choice of somebody else.
inline bool Redirect(std::uint32_t chosen,std::uint32_t replacement,std::span<const std::uint32_t> onlyFrom){
    return replacement && replacement!=chosen &&
        (onlyFrom.empty()||std::find(onlyFrom.begin(),onlyFrom.end(),chosen)!=onlyFrom.end());
}
}

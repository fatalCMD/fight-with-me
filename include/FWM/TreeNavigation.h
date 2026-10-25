#pragma once
#include <cmath>
#include <cstddef>
#include <span>
namespace FWM::TreeNavigation {
struct Node { float x; int row; bool enabled; };
constexpr int Row(std::size_t i) { return i >= 12 ? 3 : i >= 9 ? 0 : i % 3 == 0 ? 1 : 2; }
inline int Move(std::span<const Node> nodes, int current, int dx, int dy) {
    if (current < 0 || static_cast<std::size_t>(current) >= nodes.size()) return current;
    const auto from = nodes[static_cast<std::size_t>(current)];
    int result = current, bestRow = 100;
    float bestDistance = 1.0e12f;
    for (std::size_t i=0; i<nodes.size(); ++i) {
        const auto to=nodes[i];
        if (!to.enabled || static_cast<int>(i)==current) continue;
        const int rows=(to.row-from.row)*dy;
        const float distance=std::abs(to.x-from.x);
        if (dx) {
            if (to.row != from.row || (to.x-from.x)*static_cast<float>(dx)<=0) continue;
            if (distance < bestDistance) { result=static_cast<int>(i); bestDistance=distance; }
        } else if (dy && rows>0 && (rows<bestRow || (rows==bestRow && distance<bestDistance))) {
            result=static_cast<int>(i); bestRow=rows; bestDistance=distance;
        }
    }
    return result;
}
}

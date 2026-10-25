#include "FWM/PartyBalancePolicy.h"
#include <cstdio>
#include <limits>

int main()
{
    using namespace FWM::PartyBalancePolicy;
    int failures = 0;
    auto check = [&](bool pass, const char* why) { if (!pass) { ++failures; std::printf("FAIL: %s\n", why); } };
    auto close = [](float a, float b) { return std::abs(a - b) < 0.0001f; };
    check(Share(0, 1) == 1 && Share(1, 1) == 1 && Share(1, 10) == 1, "solo damage never nerfed");
    check(close(Share(4, 1), .55f), "four versus one");
    check(close(Share(4, 2), .65f), "four versus two");
    check(close(Share(4, 4), .85f), "matched groups");
    check(close(Share(20, 1), .5f), "minimum retains useful damage");
    check(close(Share(4, 0), Share(4, 1)), "missing first combat-group update is not an extra enemy penalty");
    for (int n = 1; n < 50; ++n) for (int e = 1; e < 50; ++e) {
        check(Share(n, e) >= .5f && Share(n, e) <= 1, "bounded damage");
        check(Share(n + 1, e) <= Share(n, e), "more followers never increases individual damage");
        check(Share(n, e + 1) >= Share(n, e), "more enemies relaxes the reduction");
    }
    check(Share(8, 1, 0, 0) == 1, "zero penalties disables scaling");
    check(Share(8, 1, 25, 25, 100) == 1, "100 percent floor disables scaling");
    check(Share(4, 1, std::numeric_limits<float>::quiet_NaN()) == 1, "invalid config fails open");
    check(close(Ease(.55f, .85f, 1), .7f), "reinforcements ease in");
    check(close(Ease(.85f, .55f, 1), .7f), "enemy losses ease in");
    check(Ease(.55f, .85f, 0) == .55f && Ease(.55f, .85f, -1) == .55f, "pause and invalid time do not advance");
    check(close(Ease(.55f, .6f, 1), .6f), "no overshoot");
    return failures ? 1 : 0;
}

#include "FWM/TankPolicy.h"
#include "FWM/Paths.h"
#include <cstdio>
#include <limits>

int main()
{
	using namespace FWM::TankPolicy;
	static_assert(static_cast<int>(FWM::Paths::Ability::kBodyguard) == 2);
	static_assert(static_cast<int>(FWM::Paths::Ability::kStandFast) == 4);
	static_assert(FWM::Paths::Info(FWM::Paths::Ability::kBodyguard).name == "Weak Will");
	static_assert(FWM::Paths::Info(FWM::Paths::Ability::kStandFast).name == "Last Bastion");
	int failures = 0;
	auto check = [&](bool value, const char* why) { if (!value) { ++failures; std::printf("FAIL: %s\n", why); } };
	check(CriticalAlly(.249f, 800, 800, false, true, false, true), "living ally below quarter at boundary is eligible");
	check(!CriticalAlly(.25f, 800, 800, false, true, false, true), "exact quarter does not trigger");
	check(!CriticalAlly(.1f, 801, 800, false, true, false, true), "outside reach does not trigger");
	check(!CriticalAlly(.1f, 50, 800, true, true, false, true), "tank cannot trigger on self");
	check(!CriticalAlly(.1f, 50, 800, false, true, true, true), "downed ally does not trigger");
	check(!CriticalAlly(.1f, 50, 800, false, false, false, true), "dead ally does not trigger");
	check(!CriticalAlly(.1f, 50, 800, false, true, false, false), "no trigger out of combat");
	check(!CriticalAlly(0, 50, 800, false, true, false, true), "zero health is not living");
	const auto hit = Divide(100, .8f, true, false, false);
	check(hit.victim == 20 && hit.guardian == 80 && hit.victim + hit.guardian == 100, "final loss is conserved exactly once");
	// The engine clamp can cancel damage (god mode); no loss means no transfer.
    check(Divide(0, .8f, true, false, false).guardian == 0, "cancelled damage cannot hurt guardian");
    for (const float loss : { .25f, 1.0f, 17.5f, 1000.0f }) {
        const auto tick = Divide(loss, .8f, true, false, false);
        check(std::abs(tick.victim + tick.guardian - loss) < .0001f, "small magic ticks and large hits conserve damage");
        check(std::abs(tick.victim - loss * .2f) < .0001f, "rank three leaves twenty percent of final damage");
    }
    const auto transferred = Divide(hit.guardian, .8f, true, false, true);
	check(transferred.victim == 80 && transferred.guardian == 0, "redirect cannot recurse or receive another ability reduction");
	check(Divide(100, .8f, false, false, false).victim == 100, "invalid guardian never grants free reduction");
	check(Divide(100, .8f, true, true, false).guardian == 0, "guardian cannot redirect their own incoming hit");
	check(Divide(-20, .8f, true, false, false).victim == -20, "healing remains unchanged");
	check(Divide(100, std::numeric_limits<float>::quiet_NaN(), true, false, false).victim == 100, "invalid share cannot erase damage");
	check(Stronger(.4f, .2f) == .4f && Stronger(.3f, .4f) == .4f, "Weak Will chooses strongest rather than multiplying");
	check(std::abs(WillDamage(20, 0, true) - .8f) < .00001f && std::abs(WillDamage(30, 0, true) - .7f) < .00001f &&
		std::abs(WillDamage(40, 0, true) - .6f) < .00001f, "all ranks reduce the activating follower's damage");
	check(WillDamage(0, 40, true) == 1, "party members without their own stance remain immune to enemy tags");
	check(WillDamage(0, 0, true) == 1, "expired caster stance restores normal outgoing damage");
	check(std::abs(WillDamage(0, 40, false) - .6f) < .00001f, "enemy weakening still works");
	check(WillDamage(30, 40, false) == WillDamage(0, 40, false), "own stance and enemy tag never multiply");
	check(kWeakWillCooldown > 12 && kBastionCooldown > 14, "cooldowns exceed longest base and perk guardian durations");
	return failures ? 1 : 0;
}

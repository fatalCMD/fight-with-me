#include "FWM/AbilityPolicy.h"
#include "FWM/FreezeClock.h"
#include "FWM/FreezeMotion.h"
#include <cstdio>
#include <limits>

int main()
{
	using namespace FWM::AbilityPolicy;
	using A = FWM::Paths::Ability;
	int failures = 0;
	auto check = [&](bool ok, const char* why) { if (!ok) { ++failures; std::printf("FAIL: %s\n", why); } };
	Context c;
	c.enemies = 1; c.targetDistance = 700;
	check(!Consider(A::kBurst, c), "do not spend burst on one ordinary enemy");
	c.cluster = 2; c.enemies = 2;
	check(bool(Consider(A::kBurst, c)), "burst can exploit an actual cluster");
	c.health = 0.20f;
	check(!Consider(A::kBurst, c), "reserve a critically wounded caster's turn for defense");
	c.health = 1.0f;
	c.targetHealth = 0.20f;
	check(!Consider(A::kBurst, c), "do not burst a dying target");
	c.targetHealth = 1; c.targetDistance = 2500;
	check(!Consider(A::kBurst, c), "ranged burst needs local target");
	c = {}; c.enemies = 1; c.targetBoss = true; c.boss = true; c.targetDistance = 600;
	check(bool(Consider(A::kVolley, c)), "durable boss is worth a volley");
	c.magicka = 0.8f;
	check(!Consider(A::kMagnus, c), "do not spend Magnus with nearly full magicka");
	c.magicka = 0.35f;
	check(bool(Consider(A::kMagnus, c)), "Magnus answers sustained boss pressure");
	c = {}; c.enemies = 2; c.close = 2;
	check(!Consider(A::kChaosDown, c), "nearby enemies alone do not justify control");
	c.onParty = 2;
	check(bool(Consider(A::kChaosDown, c)), "control attackers pressuring party");
	c = {}; c.allyHealth = 0.25f;
	check(Consider(A::kHeal, c).urgent, "critical heal can rescue allies behind enemy cover");
	c.allyHealth = 0.55f; c.healLine = 0.50f;
	check(!Consider(A::kHeal, c), "respect configured healing threshold");
	c.allyDown = true;
	check(Consider(A::kHeal, c).urgent, "downed ally overrides heal threshold");
	c = {}; c.health = 0.34f;
	check(Consider(A::kSecondWind, c).urgent, "self rescue stays responsive");
	c = {}; c.enemies = 2; c.peel = true;
	check(bool(Consider(A::kTaunt, c)), "taunt peels local party pressure");
	c.peel = false;
	check(!Consider(A::kTaunt, c), "do not taunt enemies already on tank");
	c = {}; c.allyDry = true;
	check(!Consider(A::kRejuvenate, c), "do not cast magicka support outside a local fight");
	c.enemies = 3;
	check(bool(Consider(A::kRejuvenate, c)), "support a drained caster during pressure");
	c = {}; c.criticalAlly = true;
	check(Consider(A::kStandFast, c).urgent, "Last Bastion rescues a critical ally even behind enemy cover");
	c.criticalAlly = false; c.health = .1f; c.enemies = 4; c.onParty = 4;
	check(!Consider(A::kStandFast, c), "tank self health and crowd pressure alone never trigger Last Bastion");
	c = {}; c.enemies = 2; c.onSelf = 2;
	check(bool(Consider(A::kBodyguard, c)), "Weak Will responds to attackers on tank");
	c.onSelf = 0; c.onPlayer = 2; c.playerHealth = .1f;
	check(!Consider(A::kBodyguard, c), "Weak Will no longer runs old player Bodyguard trigger");
    c={};c.enemies=1;c.targetDistance=600;c.targetHealth=1;c.health=1;
    check(!Consider(A::kDragonBreath,c),"breath is saved on an ordinary isolated enemy");
    c.targetBoss=true;
    check(bool(Consider(A::kDragonBreath,c)),"breath can channel into a durable enemy");
    c.targetDistance=150;
    check(!Consider(A::kDragonBreath,c),"breath needs room in front of the head");
    c.targetDistance=600;c.health=.2f;
    check(!Consider(A::kDragonBreath,c),"critically wounded caster saves breath for a safer moment");
    c.health=1;c.targetHealth=.2f;
    check(!Consider(A::kDragonBreath,c),"breath is not spent finishing a nearly dead enemy");
    c={};c.enemies=3;c.onPlayer=3;c.threatenedCaster=true;
    check(!Consider(A::kPinningShot,c),"Windstep does not retreat just because an ally is attacked");
    c.close=1;c.onSelf=1;
    check(bool(Consider(A::kPinningShot,c)),"Windstep escapes a close personal attacker");
    c.health=.2f;
    check(Consider(A::kPinningShot,c).urgent,"critical Windstep bypasses opening delay");
    c.close=0;
    check(!Consider(A::kPinningShot,c),"distant pressure cannot waste Windstep");
	Pace pace;
	check(!pace.Ready(1, A::kBurst, 0, false), "no opening frame offensive cast");
	check(pace.Ready(1, A::kHeal, 0, true), "urgent healing can open combat");
	check(pace.Ready(1, A::kStandFast, 0, true), "critical Last Bastion bypasses normal opener delay");
	pace.Tick(6);
	check(pace.Ready(1, A::kBurst, 6, false), "first deliberate cast becomes available");
	pace.Used(1, A::kBurst);
	check(!pace.Ready(1, A::kMagnus, 6, false), "shared cooldown spans both equipped abilities");
	check(!pace.Ready(2, A::kTaunt, 6, false), "party cannot cast at same instant");
	check(!pace.Ready(1, A::kStandFast, 6, true), "guardian rescue still respects instant repeat protection");
	check(!pace.Ready(1, A::kHeal, 6, true), "emergency cannot chain from same caster instantly");
	pace.Tick(0.7f);
	check(pace.Ready(2, A::kHeal, 6.7f, true), "another healer can answer emergency promptly");
	pace.Tick(2);
	check(pace.Ready(2, A::kTaunt, 8.7f, false), "party can follow with a different ability after spacing");
	check(!pace.Ready(2, A::kBurst, 8.7f, false), "duplicate bursts wait longer across party");
	pace.Tick(7);
	check(pace.Ready(1, A::kMagnus, 16, false), "member recovers after shared pause");
	pace.Used(1, A::kHeal); // ordered casts also enter recovery, without going through Ready.
	check(!pace.Ready(1, A::kBurst, 16, false), "manual cast also pauses following automatic casts");
	pace.Clear();
	check(pace.Ready(1, A::kHeal, 0, true), "new fight/load clears transient pacing");
	check(!pace.Ready(1, A::kNone, 20, true), "invalid ability never admitted");
	using FWM::Freeze::Clock;
	Clock hold{ 0.2f, 3.3f };
	hold.Tick(0.1f); check(!hold.Active(), "stagger can start before freezing");
	hold.Tick(0.2f); check(hold.Active() && std::abs(hold.left - 3.2f) < 0.001f, "catch timing carries frame overshoot");
	hold.Extend(0.2f, 4.8f); check(hold.Active() && std::abs(hold.left - 5.0f) < 0.001f, "another bite extends without releasing held pose");
	hold.Tick(5); check(!hold.Active(), "held animation resumes when duration ends");
	hold = { 0.2f, 1.0f }; hold.Tick(2); check(!hold.Active(), "large frame never leaves expired hold active");
	for (float fps : { 30.0f, 60.0f, 144.0f }) {
		const float step = 1.0f / fps;
		check(std::abs(FWM::Freeze::AnimationStep(step, true) * fps - 0.0001f) < 0.000001f, "freeze speed independent of framerate");
		check(FWM::Freeze::AnimationStep(step, false) == step, "unheld animations untouched");
		// A moving stagger may travel before the catch, then holds its horizontal position while
		// gravity continues. When the clock expires, new engine movement resumes without a saved shove.
		Clock moving{ 0.2f, 0.8f };
		const std::array<float, 4> requested{ 120.0f, -90.0f, -30.0f, 7.0f };
		float x = 0.0f, y = 0.0f, z = 0.0f;
		bool waited = false, caught = false, resumed = false;
		for (int frame = 0; frame < static_cast<int>(fps * 2); ++frame) {
			moving.Tick(step);
			const auto velocity = FWM::Freeze::MovementStep(requested, moving.Active());
			if (moving.Active()) {
				caught = true;
				check(velocity[0] == 0 && velocity[1] == 0, "caught stagger cannot carry horizontal velocity");
			} else {
				if (caught) resumed = true; else waited = true;
				check(velocity == requested, "pending and released movement goes through unchanged");
			}
			check(velocity[2] == requested[2] && velocity[3] == requested[3], "Maw preserves vertical gravity and Havok vector bookkeeping");
			x += velocity[0] * step; y += velocity[1] * step; z += velocity[2] * step;
		}
		check(waited && caught && resumed, "movement filter covers catch and release at every tested frame rate");
		check(std::abs(x - 144.0f) <= 240.0f * step && std::abs(y + 108.0f) <= 180.0f * step,
			"held time contributes no planar travel independent of frame rate");
		check(std::abs(z + 60.0f) < 0.001f, "vertical travel continues for the whole hold");
	}
	check(FWM::Freeze::MovementStep({ -12, 24, 0, 0 }, false) == std::array<float, 4>{ -12, 24, 0, 0 },
		"release uses current movement input, not the stale velocity from before the bite");
	std::printf("Ability decisions, shared pacing, and Maw animation/movement timing: %d failures\n", failures);
	return failures ? 1 : 0;
}

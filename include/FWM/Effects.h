#pragma once

// Ability visuals from Triumvirate, Elemental Destruction Magic Redux and Vibrant Weapons,
// over Fight With Me's own: recoloured copies of vanilla shaders (a rim on every look, sparks
// and flames for the healer's and mage's auras). A look whose source mod is missing, whose mesh
// does not load, or whose art fails to show uses our own instead; the log says which at load.
// Only visual models are used, never the source spells or damaging explosion records. FWM owns
// every art/shader instance; none is saved. Main thread.

namespace FWM::Effects
{
	enum class Look : std::uint8_t
	{
		kHeld,             // an enemy a taunt holds: red, for as long as it holds (red is only ever an enemy)
		kBraced,           // the tank while the taunt's armor is on: protective aura
		kTaunt,            // a wind burst as the tank taunts
		kVolley,           // a focus shield on the archer calling a volley
		kQuarry,           // Hunter's Mark on the volley's target
		kStrike,           // lightning on a fighter's weapons as they disarm
		kShaken,           // an enemy disarmed, or staggered by a war cry: cold white
		kGuard,            // a barrier around a bodyguard setting out
		kHealAura,         // Wild Healing art on the healer and everyone it reaches
		kRejuvenateAura,   // a water swirl round the healer and everyone it reaches
		kWardAura,         // a barrier on each ward recipient
		kRally,            // a rallying roar: a warm aura on everyone it reaches
		kBerserk,          // fire on a bruiser's weapons for the fury's duration
		kPinned,           // chains binding the enemy while the pin holds
		kFocus,            // a focus shield during Deadeye
		kSurge,            // a rift aura during Arcane Surge
		kMagnus,           // resurrection art during Magnus
		kWarCry,           // a burst of wind around the bruiser
		kEarthshaker,      // rocks and dust at the moment the blow lands
		kSunderImpact,     // stone fragments off the target's armor
		kCleave,           // fire and lightning on the attacking weapons
		kRiposte,          // frost on the weapons while the counter stance lasts
		kRenewal,          // a healing trance around the healer for the channel's duration
		kSecondWind,       // a restorative aura
		kStandFast,        // a protective seal on each ally
		kChallenge,        // runes on the challenged enemy
		kArrowRain,        // a brief wind cloak while arrows are launched
		kCount
	};

	// Builds the effects and keeps them out of saves. Once, after data load.
	void Install();

	// Once, for a_seconds (0: the look's own length).
	void Flash(RE::Actor* a_actor, Look a_look, float a_seconds = 0.0f);

	// An enemy disarmed: steel sparks off the weapon hand, the clang of it, and the Disarm
	// shout's own mark on them.
	void Sparks(RE::Actor* a_actor);

	// Exactly these actors, each for the seconds given, and nobody else: an effect already on
	// is stretched or cut to fit, one no longer listed goes out at once.
	void Keep(Look a_look, std::span<const std::pair<RE::Actor*, float>> a_on);

	// Discard completed models, stale actor 3D, and weapon effects after a swap or sheathe.
	void Update();

	// Every effect out: a load, or the effects switched off.
	void Clear();
}

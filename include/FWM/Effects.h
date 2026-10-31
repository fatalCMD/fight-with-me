#pragma once

// Ability visuals from Triumvirate, Elemental Destruction Magic Redux and Vibrant Weapons,
// over Fight With Me's own: recoloured copies of vanilla shaders (a rim on every look, sparks
// and flames for the healer's and mage's auras). A look whose source mod is missing, whose mesh
// does not load, or whose art fails to show uses our own instead; the log says which at load.
// Only visual models are used, never the source spells or damaging explosion records. FWM owns
// every art/shader instance; none is saved. Main thread.

namespace FWM::Effects
{
    RE::BSTempEffectParticle* SkaldModel(RE::Actor* actor,int song,bool lute,float seconds);
    RE::BSTempEffectParticle* SkaldHit(RE::Actor* actor,std::size_t song,float seconds);
    enum class SkaldCloud { Aura, Wave, Hit };
    RE::BSTempEffectParticle* SkaldParticles(RE::Actor* actor,std::size_t song,SkaldCloud kind,int rank,const RE::NiPoint3& at);
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
		kMagnus,           // a pale blue rim while Magnus holds (its orb is Orbs')
		kWarCry,           // a burst of wind around the bruiser
		kEarthshaker,      // an earthen rim on the bruiser as they slam (the slam itself is Quakes')
		kSunderImpact,     // stone fragments off the target's armor
		kSpiritWolf,       // the fighter as their spectral wolf appears: a pale rim and frost on the weapons
		kRiposte,          // frost on the weapons while the counter stance lasts
		kRenewal,          // a healing trance around the healer for the channel's duration
		kSecondWind,       // a restorative aura
		kStandFast,        // a protective seal on each ally
		kChaosDown,        // legacy target look; ChaosPulses now owns the matching restraint aura
		kArrowRain,        // a brief wind cloak while arrows are launched
		kLevelUp,          // a level earned: golden light and rays round them
		kBleeding,         // an enemy bleeding from the wolf's bite: red, for as long as it bleeds
		kCount
	};

	// Builds the effects and keeps them out of saves. Once, after data load.
	void Install();

	// Once, for a_seconds (0: the look's own length).
	void Flash(RE::Actor* a_actor, Look a_look, float a_seconds = 0.0f);
	void FinalRank(RE::Actor* a_actor, Look a_look);

	// Small original elemental trail, moved with one actual arrow by SpellArrows.
	[[nodiscard]] RE::BSTempEffectParticle* ArrowTrail(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_at, std::size_t a_element);

	// An enemy disarmed: steel sparks off the weapon hand, the clang of it, and the Disarm
	// shout's own mark on them.
	void Sparks(RE::Actor* a_actor);

	// Kynareth's Maw's giant head (meshes\FightWithMe\SpectralWolfHead.nif, cut from the vanilla
	// wolf, furred when Fluffworks' wolf fur texture is installed, else SpectralWolfHeadPlain.nif; for
	// the third rank, a_grand, the red SpectralWolfHeadRed.nif or SpectralWolfHeadRedPlain.nif), as a
	// temporary effect at a_at facing a_heading, for a_seconds. Its "Head" and "Jaw" nodes are for the
	// caller to pose. Shown whatever the effects setting: it is the ability itself. Null when the mesh
	// is not installed or the cell is not loaded. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* Head(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_heading, float a_seconds, bool a_grand = false);

	// Taunt's spectral bear. Four independent model paths for simultaneous tanks; optional fur,
	// with a vanilla-only fallback. Head, Jaw and Wave0..2 are animated by TauntBears. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* BearHead(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds, bool a_grand, bool a_warCry = false);

	[[nodiscard]] RE::BSTempEffectParticle* BreathFlame(RE::Actor* actor, const RE::NiPoint3& at, const RE::NiMatrix3& aim, float scale, float seconds);
	[[nodiscard]] RE::BSTempEffectParticle* BreathModel(RE::Actor* a_near, std::size_t copy, const RE::NiPoint3& at, float seconds, int part, int rank=1, bool compact=false);

	// Chaos Down's ground pulse; independent copies, excluded from saves.
	[[nodiscard]] RE::BSTempEffectParticle* ChaosGround(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds, bool a_grand);
	[[nodiscard]] RE::BSTempEffectParticle* ChaosBinding(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);
	// Weak Will's steel helmet: temporary, independently posed per tank, and excluded from saves.
	[[nodiscard]] RE::BSTempEffectParticle* WeakWillHelmet(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds, bool a_grand);
	[[nodiscard]] RE::BSTempEffectParticle* BastionCastle(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_seconds, bool a_grand);

	// The healer's circle (meshes\FightWithMe\HealingCircle.nif) on the ground at a_at, for
	// a_seconds; its "Circle", "Runes", "Aura0", "Aura1", "MoteNN", and Wide Circle III's
	// "Inner", "Heart" and "GlintNN" nodes are for the caller to pose.
	// Shown whatever the effects setting, like the wolf's head. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* Circle(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_seconds);
	[[nodiscard]] RE::BSTempEffectParticle* RejuvenationCircle(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);

	// Elemental Burst's blast of an element (0 fire, 1 frost, 2 shock: meshes\FightWithMe\ElementalBlast
	// <Fire|Frost|Shock>.nif) at a_at, for a_seconds; its "Orb", "Blast" and part nodes are for the caller
	// to pose, and nothing in it shows until the caller gives it strength. Shown whatever the effects
	// setting, like the wolf's head. Never saved. a_copy picks one of the mesh's two names: a burst
	// while another of its element still plays takes the other, or the game gives it no body.
	[[nodiscard]] RE::BSTempEffectParticle* Blast(RE::Actor* a_near, std::size_t a_element, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);

	// Earthshaker's slam (meshes\FightWithMe\Earthshaker.nif) at a_at, for a_seconds: its "Quake" node
	// and the parts in it are for the caller to pose; nothing shows until it does. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* Quake(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_seconds);

	// Sunder's chest cracks and detached armor fragments. Four independently named copies; a missing
	// copy fails instead of borrowing another live mesh. Its Mark and Burst are posed by Sunders. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* SunderBreak(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);
	[[nodiscard]] RE::BSTempEffectParticle* RiposteStrike(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);
    RE::BSTempEffectParticle* Windstep(RE::Actor* actor,std::size_t copy,const RE::NiPoint3& at,float seconds);
    RE::BSTempEffectParticle* ArrowArt(RE::Actor* actor,std::size_t copy,const RE::NiPoint3& at,float seconds);
    void ArrowExplosion(RE::Actor* actor,const RE::NiPoint3& at,int rank);
    RE::BSTempEffectParticle* DisarmStrike(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);
	[[nodiscard]] RE::BSTempEffectParticle* Moon(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);
	[[nodiscard]] RE::BSTempEffectParticle* SecondWindBurst(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);
	[[nodiscard]] RE::BSTempEffectParticle* BerserkMantle(RE::Actor* actor, std::size_t copy, const RE::NiPoint3& at, float seconds);
	[[nodiscard]] RE::BSTempEffectParticle* ArcaneSurgeCircle(RE::Actor* actor, std::size_t copy, const RE::NiPoint3& at, float seconds);

	// Arrow Rain's look (meshes\FightWithMe\ArrowRain.nif, or ArrowRain2.nif for a_copy 1 while another
	// rain plays) at a_at, for a_seconds: its "Mark", "Sky", "Burst", "Flare", "ArrowNN" and "HitNN" nodes
	// are for the caller to pose; nothing shows until it does. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* Rain(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);

	// Renewal's cloak of water (meshes\FightWithMe\WaterCloak.nif, or WaterCloak2.nif for a_copy 1 while another
	// healer wears one) at a_at, for a_seconds: its "Cloak" and "WashNN" nodes are for the caller to pose;
	// nothing shows until it does. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* Cloak(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);

	// The ghostly smoke on those Kynareth's Maw's bite holds (meshes\FightWithMe\Haunt.nif, or Haunt2.nif
	// for a_copy 1 while another wolf's catch stands in it) at a_at, for a_seconds: its "HauntN" nodes are
	// for the caller to pose; nothing shows until it does. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* Haunt(RE::Actor* a_near, std::size_t a_copy, const RE::NiPoint3& a_at, float a_seconds);

	// Magnus's orb (meshes\FightWithMe\MagnusOrb.nif) at a_at, for a_seconds; its "Orb" and every part in
	// it, "TendrilN", "VeilN", "FlowNN" and "WispNN" are for the caller to pose, and nothing shows until
	// the caller gives it strength. Never saved.
	[[nodiscard]] RE::BSTempEffectParticle* Orb(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_seconds);

	// An enemy a blast struck: the element on their body (burning, frosted over, crackling) for a_seconds,
	// from our own copies of the game's hit shaders.
	void Struck(RE::Actor* a_actor, std::size_t a_element, float a_seconds);

	// Blood at a point: a gout where the wolf's jaws close (with the sound of torn flesh), or a
	// spurt from a wound still bleeding. Scaled with the wolf for the bite.
	enum class Blood : std::uint8_t
	{
		kBite,
		kBleed,
		kGore  // a big gout, for the full weight of a bite
	};
	void Spray(RE::Actor* a_near, const RE::NiPoint3& a_at, float a_heading, Blood a_kind, float a_scale = 1.0f);

	// Exactly these actors, each for the seconds given, and nobody else: an effect already on
	// is stretched or cut to fit, one no longer listed goes out at once.
	void Keep(Look a_look, std::span<const std::pair<RE::Actor*, float>> a_on);

	// Discard completed models, stale actor 3D, and weapon effects after a swap or sheathe.
	void Update();
    void Animate();  // smooth attached art; called each frame independently of status bookkeeping

	// Every effect out: a load, or the effects switched off.
	void Clear();
}

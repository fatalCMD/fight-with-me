#pragma once

#include "FWM/Party.h"
#include "FWM/PathsLink.h"

// The two roles that act on their own: a tank's taunt, and a healer's heal and Rejuvenate.

namespace FWM::Abilities
{
	struct Hold
	{
		RE::ActorHandle enemy;
		RE::ActorHandle tank;
		float           remaining{ 0.0f };
	};

	// Runs taunts, heals and Rejuvenate for everyone in the party and ages the holds.
	void Update(float a_dt);

	// Every frame of a fight: a tank with a shield raises it against arrows and spells about
	// to reach them, whatever else they are doing.
	void BlockIncoming(float a_dt);

	// The fight is over: every hold let go, and every shield and block raised for the
	// fight lowered. The combat AI did not start them and would never end them.
	void Clear();

	// The armor a taunt braces a tank with, and the quickened magicka of Rejuvenate, sit on
	// temporary modifiers, which a save keeps. So each is let go when it runs out (the armor
	// also when the fight ends, both before a load), and whatever was on when the game saved
	// goes in the co-save and comes off again after loading it.
	void UpdateBoosts(float a_dt);  // every frame, in a fight or out of it
	void Unbrace();
	void EndBoosts();
	[[nodiscard]] std::vector<std::pair<std::uint32_t, float>> Braced();       // (tank, armor)
	[[nodiscard]] std::vector<std::pair<std::uint32_t, float>> BraceTimes();   // (tank, seconds left)
	[[nodiscard]] std::vector<std::pair<std::uint32_t, float>> Rejuvenated();  // (companion, percent)
	[[nodiscard]] float RejuvenatedFor(RE::FormID a_id);                       // seconds left, 0 for none
	struct BoostTime
	{
		float amount{ 0.0f };
		float left{ 0.0f };
		float length{ 0.0f };
	};
	[[nodiscard]] std::optional<BoostTime> BoostOn(RE::FormID a_id, RE::ActorValue a_value);  // kDamageResist: braced, kMagickaRateMult: rejuvenated
	[[nodiscard]] std::optional<BoostTime> BoostOn(RE::FormID a_id, RE::ActorValue a_value, std::uint16_t a_source);
	void LoadBraced(const std::vector<std::pair<std::uint32_t, float>>& a_saved);
	void LoadRejuvenated(const std::vector<std::pair<std::uint32_t, float>>& a_saved);

	// Any ability's lift on a value, by what gave it (a Paths::Perk, or 0 for a taunt's armor
	// and Rejuvenate without a path): on, or on again with the clock started over. A negative
	// amount lowers it (an enemy's armor, their damage). Each comes off on its own clock, and
	// whatever is on when the game saves goes in the co-save and comes off after the load.
	void Lift(RE::FormID a_id, RE::ActorValue a_value, float a_amount, float a_seconds, std::uint16_t a_source = 0);
	// Kept up for as long as it keeps being asked for: a trait or a condition checked on every
	// tick. An amount of 0 takes it off.
	void Keep(RE::FormID a_id, RE::ActorValue a_value, float a_amount, float a_seconds, std::uint16_t a_source);
	void DropSource(std::uint16_t a_source, std::optional<RE::FormID> a_id = std::nullopt, std::optional<RE::ActorValue> a_value = std::nullopt);

	struct SavedBoost
	{
		std::uint32_t id{ 0 };
		std::uint32_t value{ 0 };
		float         amount{ 0.0f };
	};
	[[nodiscard]] std::vector<SavedBoost> Boosts();
	void                                  LoadBoosts(const std::vector<SavedBoost>& a_saved);

	[[nodiscard]] const std::vector<Hold>& Holds();

	// An enemy on a tank for a while, as a taunt that took would hold them (Challenge).
	void AddHold(const RE::ActorHandle& a_enemy, const RE::ActorHandle& a_tank, float a_seconds);

	// The tank currently holding this enemy, if any.
	[[nodiscard]] RE::ActorHandle HolderOf(const RE::ActorHandle& a_enemy);

	struct TauntPreview
	{
		float radius{ 0.0f };
		int   certainUpTo{ 0 };  // enemy level held for certain
		int   noChanceFrom{ 0 };  // enemy level that can never be held
		float hold{ 0.0f };
		float cooldown{ 0.0f };
		float stamina{ 0.0f };
		float armor{ 0.0f };         // braced with after each taunt
		float armorSeconds{ 0.0f };  // as long as the taunt holds
	};

	[[nodiscard]] TauntPreview PreviewTaunt(RE::Actor* a_tank);

	// Shared with the specials.
	[[nodiscard]] bool Busy(RE::Actor* a_actor);  // mid-swing, staggered, down, in a kill move
	[[nodiscard]] bool WaitedTooLong(const Party::Member& a_member);  // asked more than a moment ago
	void               Play(RE::BGSSoundDescriptorForm* a_sound, RE::Actor* a_at);
	// Through the actor's AI, which plays an idle only when its conditions and the moment
	// allow. Nothing is forced onto the behaviour graph.
	bool               PlayIdle(RE::Actor* a_actor, RE::TESIdleForm* a_idle);
	// A taunt or war cry to go with it: the shield bang with a shield, otherwise the cheer
	// or the raised weapon.
	bool               Challenge(RE::Actor* a_actor);
	// A healer's or mage's ability: the gesture of someone working magic with it in hand.
	bool               CastGesture(RE::Actor* a_caster);
	void               HandGlow(RE::Actor* a_caster, RE::SpellItem* a_spell, float a_seconds, std::size_t a_hand = kLeftHand);  // the spell's casting art on a hand
	// An ability that worked. A companion with unlimited points sometimes clucks as they use it.
	void               Used(RE::FormID a_member, RE::Actor* a_actor);
}

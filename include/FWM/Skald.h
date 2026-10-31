#pragma once
#include "FWM/PathsLink.h"
#include "FWM/View.h"
#include <optional>

namespace FWM::Skald {
bool HasLute(RE::Actor* actor);
bool IsSong(Paths::Ability ability);
bool Performing(RE::FormID id);
// Main thread: basic notes may yield to a real Ljodh, which must never interrupt another Ljodh.
bool Singing(RE::FormID id);
// Main-thread support destination, shared through the existing steering bridge.
// A bard fights like a healer, near the player: Guard and Hold back keep their own
// leash, and Free, Front line or With Me all keep it with you as With Me does.
inline Position SupportPosition(Position a_position){
    return a_position==Position::kGuard||a_position==Position::kHoldBack?a_position:Position::kWithMe;
}
bool Relocating(RE::FormID id);
bool FistsReady(RE::Actor* actor);
// The combat AI may hold the stance spell (SkaldPolicy::kStanceSpell): in a fight
// and not standing for a Ljodh.
bool CasterStance(RE::Actor* actor);
// Before the game saves: the lent stance spell is taken back, and lent again after.
void BeforeSave();
// Read-only eligibility snapshot for Walk With Me's own tavern rest scheduler.
bool CanTavernLute(RE::FormID actor);
void OnHit(const RE::TESHitEvent* event);
bool Cast(RE::Actor* actor,const PathsLink::Kit& kit,Paths::Ability ability);
bool Useful(RE::Actor* actor,Paths::Ability ability);
void Tick(float seconds);
void EndCombat();
void Clear();
void Statuses(RE::FormID id,std::vector<View::Status>& out);
std::vector<std::pair<std::uint32_t,float>> SavedPerformers();
void LoadPerformers(const std::vector<std::pair<std::uint32_t,float>>& saved);
void Install();
float Recovery(RE::FormID id);
float ResourceFactor(RE::FormID id,Paths::Ability ability);
void ConsumeInspiration(RE::FormID id,Paths::Ability ability);
float Outgoing(RE::Actor* attacker,RE::Actor* target,float damage,bool direct,bool magic);
float Absorb(RE::Actor* target,float damage);
float CastingSpeed(RE::FormID id);
float PowerCost(RE::FormID id);
// A discord icon over a struck enemy, placed for the HUD: screen fractions, age and
// lifetime in seconds, an icon-font glyph, a packed RGBA colour and the stack count.
// A major mark is a crescendo: larger, with a ring counting down its hold.
struct NoteMark { float x{},y{},age{},life{1.1f}; std::uint32_t glyph{},color{}; unsigned count{},slot{},slots{1}; bool major{}; };
std::vector<NoteMark> Marks();
// Any thread, right after Skyrim writes an actor's pose for the frame.
void AfterAnimation(RE::Actor* actor);
}

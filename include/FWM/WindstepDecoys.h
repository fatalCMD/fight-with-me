#pragma once
namespace FWM::WindstepDecoys {
// Invisible, attackable actors under the spectral art. They never fight or join the roster.
RE::ActorHandle Create(RE::Actor* caster,const RE::NiPoint3& at);
bool Engage(RE::Actor* enemy,RE::Actor* echo);
void Drop(RE::ActorHandle& actor);
bool Is(RE::FormID actor); // live or spare echo; thread-safe; also exported to the separate orders plugin
}

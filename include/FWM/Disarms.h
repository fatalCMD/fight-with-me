#pragma once
namespace FWM::Disarms {
// Capture the drawn weapon contact point before the engine queues its drop.
bool Strike(RE::Actor* target,int rank);
void Tick(float dt);
void Clear();
}

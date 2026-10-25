#pragma once

// The healer's circle in the world: raised on the ground where it helps most, it heals everyone of
// the party standing in it a little every moment (their stamina too, with Invigorate), burns the
// undead in it, and draws hurt companions into it. Its look is meshes\FightWithMe\HealingCircle.nif,
// posed every frame from HealCircle.h; an invisible marker at its middle is what the companions'
// combat areas are centred on. Neither is ever saved. Main thread.

namespace FWM::Circles
{
	// How strong one circle is. Each per-second amount is a share of the one it touches plus a flat
	// part: the paths give shares, the classic heal a flat amount.
	struct Strength
	{
		float healShare{ 0.0f };   // of each one's own health, a second
		float healFlat{ 0.0f };    // health a second
		float staminaShare{ 0.0f };
		float burnShare{ 0.0f };   // of each undead's health, a second (a boss takes half)
		float burnFlat{ 0.0f };
		float triage{ 0.0f };      // more for anyone below a third of their health
		float bonus{ 1.0f };       // the healer's path bonus to healing
		float radius{ 0.0f };
		float seconds{ 0.0f };
		bool  grand{ false };  // Wide Circle III: an inner circle, a white heart and specks of light all over it
	};

	// Raises one for this healer, centred on a_on (an ally standing where it helps most). True when
	// it stands, whether or not its look could be shown.
	bool Raise(RE::FormID a_healer, std::string a_name, RE::Actor* a_on, const Strength& a_strength);

	// The healer's circle still stands.
	[[nodiscard]] bool Standing(RE::FormID a_healer);

	// Every frame of a fight: healing, burning, the look, and who it draws in.
	void Tick(float a_dt);

	// Where a companion is being drawn to, for their combat area: the marker and how close to it.
	struct Pull
	{
		RE::TESObjectREFR* centre{ nullptr };
		float              radius{ 0.0f };
	};
	[[nodiscard]] std::optional<Pull> PullFor(RE::Actor* a_member);

	// The fight is over, or a load: every circle and marker gone.
	void Clear();
}

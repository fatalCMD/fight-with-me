#pragma once

// While the battle plan is open the camera eases over to the companion being
// commanded, glides between companions as the selection changes, and eases back
// to the player's own view when the plan closes.
//
// The pose is written after ThirdPersonState::Update, so the game's camera work for
// the frame is done and this write is the last one. SmoothCam is asked to hand over
// control through its API for as long as the glide lasts.
//
// The player can turn the view: around the chosen companion, or, overhead, around the
// whole fight. Overhead indoors, everything closer to the camera than the tops of the
// fighters' heads is clipped by the near plane, which takes the ceiling off without
// touching a single reference.

namespace FWM::Camera
{
	void RegisterSmoothCam();  // kPostLoad: SmoothCam answers once every plugin is loaded
	void RequestSmoothCam();   // kDataLoaded
	void Install();            // kDataLoaded

	// Main thread.
	void Begin(bool a_treeOnly = false);
	void End();
	void Reset();  // a load or new game: let go at once

	// Any thread: the companion to frame, or 0 for the player's own view. A portrait is
	// face to face and close, for when no fight is on.
	void Focus(RE::FormID a_actor, bool a_portrait = false) noexcept;
	// Any thread: upward perk-tree entrance, without altering the saved camera orbit.
	void TreeLift(float a_amount) noexcept;

	// Any thread. Turns the view by these angles in radians (yaw to the right, pitch up),
	// and zooms by steps (positive is closer).
	void Turn(float a_yaw, float a_pitch) noexcept;
	void Zoom(float a_steps) noexcept;

	// Any thread, overhead only: slide the view across the battlefield, in screen heights
	// (positive is right and away), and bring it back over the fight.
	void Pan(float a_right, float a_forward) noexcept;
	void Recentre() noexcept;

	// Any thread: the whole fight from above instead of the chosen companion.
	void SetOverhead(bool a_on) noexcept;
	[[nodiscard]] bool Overhead() noexcept;

	// Whether the camera follows the plan at all (the glide setting, and nothing holding
	// the camera that would not hand it over).
	[[nodiscard]] bool Driving() noexcept;
}

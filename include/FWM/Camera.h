#pragma once

// While the battle plan is open the camera eases over to the companion being
// commanded, glides between companions as the selection changes, and eases back
// to the player's own view when the plan closes.
//
// The pose is written after ThirdPersonState::Update, so the game's camera work for
// the frame is done and this write is the last one. SmoothCam is asked to hand over
// control through its API for as long as the glide lasts.

namespace FWM::Camera
{
	void RegisterSmoothCam();  // kPostLoad: SmoothCam answers once every plugin is loaded
	void RequestSmoothCam();   // kDataLoaded
	void Install();            // kDataLoaded

	// Main thread.
	void Begin();
	void End();
	void Reset();  // a load or new game: let go at once

	// Any thread: the companion to frame, or 0 for the player's own view. A portrait is
	// face to face and close, for when no fight is on.
	void Focus(RE::FormID a_actor, bool a_portrait = false) noexcept;
}

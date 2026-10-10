#pragma once

// The battle plan (an SKSE Menu Framework window), the HUD notices and the
// Menu Framework section. Without Menu Framework 3 none of this registers and
// the plugin runs on saved orders and INI defaults.

namespace FWM::Menu
{
	void Register();  // at data load

	[[nodiscard]] bool Available() noexcept;

	// Safe from any thread.
	void Open();
	void Close();
	[[nodiscard]] bool IsOpen() noexcept;
}

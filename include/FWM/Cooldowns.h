#pragma once

#include <cstdint>
#include <span>
#include <vector>

// Ability cooldowns run on world time, in a fight or out of it, and go into the co-save. No
// game types here, so the clock and the save record are tested.

namespace FWM::Cooldowns
{
	// A wait still running: whose, which ability, the seconds left, and the whole wait (for the
	// ring on the badge).
	struct Wait
	{
		std::uint32_t id{ 0 };
		std::uint8_t  key{ 0 };
		float         left{ 0.0f };
		float         length{ 0.0f };

		friend bool operator==(const Wait&, const Wait&) = default;
	};

	// Keys: the abilities every role had before the paths have these; the ones only a path
	// brings use their Paths::Ability value, which stays below them.
	inline constexpr std::uint8_t kTaunt = 0x80;
	inline constexpr std::uint8_t kHeal = 0x81;
	inline constexpr std::uint8_t kRejuvenate = 0x82;
	inline constexpr std::uint8_t kSpecial = 0x83;

	// No ability waits longer; a saved wait past it is a damaged record.
	inline constexpr float kLongest = 3600.0f;

	inline constexpr std::uint32_t kVersion = 1;
	[[nodiscard]] std::vector<std::uint8_t> Encode(std::span<const Wait> a_waits);
	// Waits that are over, or make no sense, are left out.
	[[nodiscard]] std::vector<Wait> Decode(std::span<const std::uint8_t> a_bytes, std::uint32_t a_version);

	// One step of a countdown.
	[[nodiscard]] float Down(float a_left, float a_seconds);

	// World seconds since the last frame: the frame's own, plus any move of the calendar that
	// no frame accounts for (waiting, sleeping, fast travel), so an hour's wait ends every wait.
	class Clock
	{
	public:
		// a_frame: the frame's world seconds; a_days: GameDaysPassed; a_hour: GameHour (0-24);
		// a_timescale: game seconds to a world second. The days only count whole days: as a float
		// they lose the minutes late in a game, which the hour keeps.
		float Advance(float a_frame, float a_days, float a_hour, float a_timescale);
		// A load or a new game: the calendar is someone else's now.
		void Restart();

		// The calendar has to run this many seconds past the frames before it counts as a jump,
		// and the frames are counted again from the calendar this often.
		static constexpr float kJump = 20.0f;
		static constexpr float kRecount = 60.0f;

		// Game hours from one reading of the calendar to another.
		[[nodiscard]] static double HoursBetween(float a_days, float a_hour, float a_toDays, float a_toHour);

	private:
		float days{ -1.0f };    // the calendar when the frames were last counted from
		float hour{ 0.0f };
		float counted{ 0.0f };  // world seconds the frames have given since
	};
}

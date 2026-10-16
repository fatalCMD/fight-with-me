#include "FWM/Cooldowns.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace FWM::Cooldowns
{
	namespace
	{
		constexpr std::size_t kWaitBytes = 13;  // id 4, key 1, left 4, length 4
	}

	std::vector<std::uint8_t> Encode(std::span<const Wait> a_waits)
	{
		std::vector<std::uint8_t> out;
		out.reserve(4 + a_waits.size() * kWaitBytes);
		auto put = [&](const void* a_value, std::size_t a_size) {
			const auto* bytes = static_cast<const std::uint8_t*>(a_value);
			out.insert(out.end(), bytes, bytes + a_size);
		};
		const auto count = static_cast<std::uint32_t>(a_waits.size());
		put(&count, 4);
		for (const auto& wait : a_waits) {
			put(&wait.id, 4);
			put(&wait.key, 1);
			put(&wait.left, 4);
			put(&wait.length, 4);
		}
		return out;
	}

	std::vector<Wait> Decode(std::span<const std::uint8_t> a_bytes, std::uint32_t a_version)
	{
		std::vector<Wait> out;
		if (a_version < 1 || a_bytes.size() < 4) {
			return out;
		}
		std::uint32_t count = 0;
		std::memcpy(&count, a_bytes.data(), 4);
		count = static_cast<std::uint32_t>(std::min<std::size_t>(count, (a_bytes.size() - 4) / kWaitBytes));
		for (std::uint32_t n = 0; n < count; ++n) {
			const auto* at = a_bytes.data() + 4 + n * kWaitBytes;
			Wait        wait;
			std::memcpy(&wait.id, at, 4);
			std::memcpy(&wait.key, at + 4, 1);
			std::memcpy(&wait.left, at + 5, 4);
			std::memcpy(&wait.length, at + 9, 4);
			if (!wait.id || !std::isfinite(wait.left) || wait.left <= 0.0f || wait.left > kLongest) {
				continue;
			}
			if (!std::isfinite(wait.length) || wait.length < wait.left || wait.length > kLongest) {
				wait.length = wait.left;
			}
			out.push_back(wait);
		}
		return out;
	}

	float Down(float a_left, float a_seconds)
	{
		return std::max(a_left - std::max(a_seconds, 0.0f), 0.0f);
	}

	double Clock::HoursBetween(float a_days, float a_hour, float a_toDays, float a_toHour)
	{
		const double hours = static_cast<double>(a_toHour) - static_cast<double>(a_hour);
		const double rough = (static_cast<double>(a_toDays) - static_cast<double>(a_days)) * 24.0;
		return std::round((rough - hours) / 24.0) * 24.0 + hours;
	}

	float Clock::Advance(float a_frame, float a_days, float a_hour, float a_timescale)
	{
		const float frame = std::isfinite(a_frame) ? std::max(a_frame, 0.0f) : 0.0f;
		const bool  readable = std::isfinite(a_days) && std::isfinite(a_hour) && std::isfinite(a_timescale) && a_timescale > 0.0f && a_days >= 0.0f;
		const double moved = readable && days >= 0.0f ? HoursBetween(days, hour, a_days, a_hour) * 3600.0 / a_timescale : -1.0;
		if (moved < 0.0) {
			// Nothing to count from yet, or the calendar went back: a load we were not told of.
			days = readable ? a_days : -1.0f;
			hour = a_hour;
			counted = 0.0f;
			return frame;
		}
		counted += frame;
		if (moved > counted + kJump) {
			const auto jump = static_cast<float>(moved) - counted;
			days = a_days;
			hour = a_hour;
			counted = 0.0f;
			return frame + jump;
		}
		// The frames and the calendar drift apart a little; counting again from the calendar now
		// and then keeps that to a minute's worth.
		if (counted > kRecount) {
			days = a_days;
			hour = a_hour;
			counted = 0.0f;
		}
		return frame;
	}

	void Clock::Restart()
	{
		days = -1.0f;
		hour = 0.0f;
		counted = 0.0f;
	}
}

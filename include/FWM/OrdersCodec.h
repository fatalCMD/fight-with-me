#pragma once

#include "FWM/Orders.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// The co-save record for orders: a flat list of (form id, orders). Kept free of
// SKSE so it can be round-tripped in the tests.

namespace FWM::Codec
{
	inline constexpr std::uint32_t kVersion = 1;

	using Entry = std::pair<std::uint32_t, Orders>;

	[[nodiscard]] std::vector<std::uint8_t> Encode(std::span<const Entry> a_entries);

	// Unknown enum values are clamped to defaults rather than dropping the entry,
	// so a save made by a newer version still loads what it can.
	[[nodiscard]] std::vector<Entry> Decode(std::span<const std::uint8_t> a_bytes, std::uint32_t a_version);
}

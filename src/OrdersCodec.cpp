#include "FWM/OrdersCodec.h"

#include <algorithm>

namespace FWM::Codec
{
	namespace
	{
		constexpr std::size_t kEntrySize = 9;  // id(4) role focus position flags threshold

		// The flags byte. Switches added later are stored as "off", so a save made before
		// them reads as on, and an older version reading a newer save sees only bit 0.
		constexpr std::uint8_t kAuto = 1 << 0;
		constexpr std::uint8_t kSpecialOff = 1 << 1;
		constexpr std::uint8_t kPotionsOff = 1 << 2;

		template <class E>
		E Clamp(std::uint8_t a_value, E a_fallback)
		{
			return a_value < static_cast<std::uint8_t>(E::kCount) ? static_cast<E>(a_value) : a_fallback;
		}
	}

	std::vector<std::uint8_t> Encode(std::span<const Entry> a_entries)
	{
		std::vector<std::uint8_t> out;
		out.reserve(4 + a_entries.size() * kEntrySize);
		const auto count = static_cast<std::uint32_t>(a_entries.size());
		for (int i = 0; i < 4; ++i) {
			out.push_back(static_cast<std::uint8_t>(count >> (8 * i)));
		}
		for (const auto& [id, orders] : a_entries) {
			for (int i = 0; i < 4; ++i) {
				out.push_back(static_cast<std::uint8_t>(id >> (8 * i)));
			}
			out.push_back(static_cast<std::uint8_t>(orders.role));
			out.push_back(static_cast<std::uint8_t>(orders.focus));
			out.push_back(static_cast<std::uint8_t>(orders.position));
			out.push_back(static_cast<std::uint8_t>((orders.autoAbility ? kAuto : 0) | (orders.autoSpecial ? 0 : kSpecialOff) | (orders.potions ? 0 : kPotionsOff)));
			out.push_back(orders.healThreshold);
		}
		return out;
	}

	std::vector<Entry> Decode(std::span<const std::uint8_t> a_bytes, std::uint32_t a_version)
	{
		std::vector<Entry> out;
		if (a_version < 1 || a_bytes.size() < 4) {
			return out;
		}
		std::uint32_t count = 0;
		for (int i = 0; i < 4; ++i) {
			count |= static_cast<std::uint32_t>(a_bytes[i]) << (8 * i);
		}
		const std::size_t available = (a_bytes.size() - 4) / kEntrySize;
		count = static_cast<std::uint32_t>(std::min<std::size_t>(count, available));
		out.reserve(count);
		const Orders defaults{};
		for (std::uint32_t n = 0; n < count; ++n) {
			const auto*   p = a_bytes.data() + 4 + n * kEntrySize;
			std::uint32_t id = 0;
			for (int i = 0; i < 4; ++i) {
				id |= static_cast<std::uint32_t>(p[i]) << (8 * i);
			}
			Orders orders;
			orders.role = Clamp(p[4], defaults.role);
			orders.focus = Clamp(p[5], defaults.focus);
			orders.position = Clamp(p[6], defaults.position);
			orders.autoAbility = (p[7] & kAuto) != 0;
			orders.autoSpecial = (p[7] & kSpecialOff) == 0;
			orders.potions = (p[7] & kPotionsOff) == 0;
			orders.healThreshold = static_cast<std::uint8_t>(std::clamp<int>(p[8], 10, 95));
			out.emplace_back(id, orders);
		}
		return out;
	}
}

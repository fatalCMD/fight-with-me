#pragma once

// Orders, pools, ability lifts, cooldowns and levels in the SKSE co-save. Fight With Me keeps
// the orders and pools in its own; Path of the Companion keeps the rest in its own, beside the
// paths, with its own copy of the orders for the abilities' switches (and, on its own, the pools).

namespace FWM::Save
{
	// Fight With Me's own co-save.
	void Register();

	// The same records in Path of the Companion's co-save. Read takes one record and says whether
	// it was one of these.
	void Write(SKSE::SerializationInterface* a_intfc);
	bool Read(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, const std::vector<std::uint8_t>& a_bytes);
	void Revert();

	// Path of the Companion: a record Fight With Me found in its part of a save made before the
	// abilities moved out of it. Orders only for companions it has none for.
	void Adopt(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, const std::vector<std::uint8_t>& a_bytes);
}

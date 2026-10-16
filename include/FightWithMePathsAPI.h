#pragma once

#include <cstdint>

// Paths - Follower Ability Skilltree, the perk tree add-on. Fight With Me resolves
// FightWithMePaths_GetInterface from FightWithMePaths.dll with GetProcAddress and draws the
// trees in its battle plan; the add-on keeps the points, the ranks and the save record.
// Every call is main thread only.

namespace FightWithMePathsAPI
{
	inline constexpr std::uint32_t INTERFACE_VERSION = 3;

	inline constexpr std::uint32_t kRoles = 6;   // Fighter, Tank, Healer, Bruiser, Archer, Mage
	inline constexpr std::uint32_t kNodes = 15;  // per path, in FWM::Paths order
	inline constexpr std::uint32_t kSlots = 2;

	// A copy of one companion's progress.
	struct Record
	{
		std::uint8_t  ranks[kRoles][kNodes];
		std::uint8_t  slots[kRoles][kSlots];  // FWM::Paths::Ability values
		std::uint16_t peak;                   // highest level seen
		std::uint16_t flags;                  // kUnlimitedFlag: unlimited points are on
		std::int32_t  earned;                 // points earned in all, by this companion alone
		std::int32_t  spent;                  // points spent on every path, bonus points included
		std::uint16_t bonus[kRoles];          // points put straight into each path's bonus
	};

	inline constexpr std::uint16_t kUnlimitedFlag = 1;

	// FWM::Paths::Block values: 0 is done, anything else is why not.
	using Result = std::int32_t;

	class IPaths
	{
	public:
		virtual ~IPaths() = default;
		[[nodiscard]] virtual std::uint32_t GetVersion() const noexcept = 0;
		// FWM::Paths::Signature() of the trees the add-on was built with: Fight With Me only
		// talks to an add-on whose trees match its own.
		[[nodiscard]] virtual std::uint32_t GetTreeSignature() const noexcept = 0;
		// The record for an actor (reference form id), made on first sight: the defaults, and
		// every point their level has already earned. Reading also notes a higher level.
		virtual bool Read(std::uint32_t a_actor, Record* a_out) = 0;
		virtual Result Learn(std::uint32_t a_actor, std::uint32_t a_role, std::uint32_t a_node) = 0;
		virtual Result Forget(std::uint32_t a_actor, std::uint32_t a_role, std::uint32_t a_node) = 0;
		virtual Result Reset(std::uint32_t a_actor, std::uint32_t a_role) = 0;
		virtual Result Equip(std::uint32_t a_actor, std::uint32_t a_role, std::uint32_t a_slot, std::uint32_t a_ability) = 0;
		// A point for every level past this one.
		[[nodiscard]] virtual std::int32_t PointsAboveLevel() const = 0;
		// Whether ranks can be given back.
		[[nodiscard]] virtual bool RespecAllowed() const = 0;
		// Unlimited points for one companion. Switched on, their record as it is now is kept aside;
		// switched off, they go back to it: every rank bought in the meantime is gone, and what they
		// carry stays only where it is still learned.
		virtual Result SetUnlimited(std::uint32_t a_actor, bool a_on) = 0;
		[[nodiscard]] virtual bool UnlimitedAllowed() const = 0;
		// A leftover point straight into the bonus of a path every node of which is mastered.
		// a_room: how many more the bonus still takes, which only Fight With Me knows.
		virtual Result Invest(std::uint32_t a_actor, std::uint32_t a_role, std::int32_t a_room) = 0;
		// One point back out of a path's bonus; like giving back a rank.
		virtual Result Divest(std::uint32_t a_actor, std::uint32_t a_role) = 0;
	};
}

#if defined(FIGHTWITHMEPATHS_EXPORTS)
#	define FIGHTWITHMEPATHS_API __declspec(dllexport)
#else
#	define FIGHTWITHMEPATHS_API __declspec(dllimport)
#endif

extern "C" FIGHTWITHMEPATHS_API FightWithMePathsAPI::IPaths* FightWithMePaths_GetInterface(std::uint32_t a_version);

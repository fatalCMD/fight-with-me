#pragma once

#include <cstdint>

// Fight With Me's public interface. Resolve FightWithMe_GetInterface from
// FightWithMe.dll with GetProcAddress; every call is safe from the main thread.

namespace FightWithMeAPI
{
	inline constexpr std::uint32_t INTERFACE_VERSION = 1;

	enum class Role : std::int32_t
	{
		kNone = -1,
		kFighter = 0,
		kTank = 1,
		kHealer = 2,
		kBruiser = 3,
		kArcher = 4,
		kMage = 5
	};

	class IFightWithMe
	{
	public:
		virtual ~IFightWithMe() = default;
		[[nodiscard]] virtual std::uint32_t GetVersion() const noexcept = 0;
		virtual void                        OpenBattlePlan() = 0;
		[[nodiscard]] virtual bool          IsBattlePlanOpen() const = 0;
		// The role saved for a follower (actor reference form id), or kNone.
		[[nodiscard]] virtual Role GetRole(std::uint32_t a_actorFormID) const = 0;
	};
}

#if defined(FIGHTWITHME_EXPORTS)
#define FIGHTWITHME_API __declspec(dllexport)
#else
#define FIGHTWITHME_API __declspec(dllimport)
#endif

extern "C" FIGHTWITHME_API FightWithMeAPI::IFightWithMe* FightWithMe_GetInterface(std::uint32_t a_version);
extern "C" FIGHTWITHME_API bool                          FightWithMe_OpenBattlePlan();
// Companions are in a fight Fight With Me runs, or have just been called into one. Walk With
// Me leaves the party alone while this holds. Safe from any thread.
extern "C" FIGHTWITHME_API bool                          FightWithMe_InBattle();
// The enemy a follower (actor reference form id) was sent after in the battle plan, as a native
// actor handle; 0 when they were sent after no one. Main thread.
extern "C" FIGHTWITHME_API std::uint32_t                 FightWithMe_GetMark(std::uint32_t a_actorFormID);
// Saved combat position (FWM::Position value), or -1 if no orders exist. Main thread.
// Optional export; does not change the version-1 virtual interface.
extern "C" FIGHTWITHME_API std::int32_t                  FightWithMe_GetPosition(std::uint32_t a_actorFormID);

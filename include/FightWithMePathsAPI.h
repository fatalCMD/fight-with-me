#pragma once

#include <cstdint>

// Path of the Companion (FightWithMePaths.dll): every companion's paths, abilities, traits and
// levels. Fight With Me resolves FightWithMePaths_GetInterface with GetProcAddress. It runs no
// ability itself: once a planning tick it asks what the abilities running now need of the fight
// (Steering, Holds) and carries that out with its own orders, so only one plugin ever steers a
// companion. Every call is main thread only, but DrawInPlan.

namespace FightWithMePathsAPI
{
	inline constexpr std::uint32_t INTERFACE_VERSION = 6;

	inline constexpr std::uint32_t kRoles = 7;   // Fighter, Tank, Healer, Bruiser, Archer, Mage
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
    inline constexpr std::uint16_t kSkaldUnlockedFlag = 2;

	// What the abilities on one companion ask of their fight right now.
	struct Steer
	{
		std::uint32_t actor;   // reference form id
		std::uint32_t target;  // native actor handle of an enemy to turn on (a volley), 0 for none
		std::uint32_t circle;  // native reference handle of a healing circle's centre to walk into, 0 for none
		float         radius;  // that circle's
		// How their path and the abilities on them change the way they fight: multipliers on a
		// combat style's values, and offensive and defensive added on top.
		float bash, power, flank, circling, fallback, strafe, avoidThreat, offensive, defensive;
	};

	// An enemy a taunt holds on a tank: native actor handles.
	struct Hold
	{
		std::uint32_t enemy;
		std::uint32_t tank;
	};

	// One frame of Fight With Me's battle plan, which Path of the Companion draws into: the
	// abilities where the plan's ability dock was, or the path tree over the whole plan.
	struct Plan
	{
		// Fight With Me's.
		std::uint32_t member;       // the companion the plan shows, reference form id; 0 for none
		float         x, y, width;  // the dock's top left and width, in pixels
		float         scale;        // the plan's scale, and the tree's
		float         treeScale;
		float         appear;  // 0..1 as the plan fades in and out
		bool          opened;  // the plan has just opened
		bool          closing;
		bool          focused;  // the pad's cursor is on the abilities
		bool          keyboardNav;
		std::int32_t  enter;   // the cursor came onto them this frame: 1 from above, -1 from below
		std::int32_t  dx, dy;  // the pad and the arrows, while focused
		bool          activate;  // A or Enter, while focused
		bool          now;       // X: the first ability now
		bool          flipAuto;  // Y: its AUTO switch
		// Path of the Companion's answer.
		float         min[2], max[2];  // the dock as drawn; all 0 for none
		std::int32_t  exit;            // the cursor left the dock: -1 up, 1 down
		bool          tree;            // the tree is open: it covers the plan and reads the pad and keys itself
		float         lift;            // the camera's rise for the tree, 0..1
		std::uint32_t select;          // a companion the tree turned to, for the plan to show; 0 for none
	};

	// A companion's level earned in battle, for Fight With Me's plan, which keeps none itself.
	struct Level
	{
		std::int32_t level;
		float        xp;            // toward the next level
		float        next;          // what the next level costs
		float        sinceLevelUp;  // seconds since the last one, 99 when long ago
		bool         capped;        // at or above the player's level: no XP until they level up
	};

	// FWM::Paths::Block values: 0 is done, anything else is why not.
	// Optional status export: fixed buffers only, no allocator or STL objects cross DLLs.
	struct Status
	{
		char name[128]{};
		char detail[512]{};
		std::uint32_t glyph{}, tint{};
		float left{-1.f}, length{}, progress{-1.f};
	};

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
		// Every companion something is asked for this moment, up to a_capacity; returns how many
		// were written. Call once a planning tick.
		virtual std::uint32_t Steering(Steer* a_out, std::uint32_t a_capacity) = 0;
		virtual std::uint32_t Holds(Hold* a_out, std::uint32_t a_capacity) = 0;
		// A record from Fight With Me's own part of a save made before abilities moved here: the
		// AUTO switches, cooldowns, levels earned by fighting, and the orders' ability settings.
		// Only from inside Fight With Me's load callback; a_serialization is SKSE's interface.
		virtual void Adopt(void* a_serialization, std::uint32_t a_type, std::uint32_t a_version, const std::uint8_t* a_data, std::uint32_t a_size) = 0;
		// Render thread, inside the plan's window, once a frame while the plan is open.
		virtual void DrawInPlan(Plan* a_plan) = 0;
	};
}

#if defined(FIGHTWITHMEPATHS_EXPORTS)
#	define FIGHTWITHMEPATHS_API __declspec(dllexport)
#else
#	define FIGHTWITHMEPATHS_API __declspec(dllimport)
#endif

extern "C" FIGHTWITHMEPATHS_API FightWithMePathsAPI::IPaths* FightWithMePaths_GetInterface(std::uint32_t a_version);

// Optional additive export. The assigned role is already shared through the orders
// bridge. Paths supplies compatible weapon kinds; Fight With Me owns pool storage,
// selection and equipment. Bits: one-handed, two-handed, bow, staff, shield.
extern "C" FIGHTWITHMEPATHS_API std::uint32_t FightWithMePaths_GetLoadoutMask(std::uint32_t a_role);

// Optional additive export, main thread only: a companion's battle level (reference form id).
// False while leveling is off or nobody by that id has been taken up.
extern "C" FIGHTWITHMEPATHS_API bool FightWithMePaths_GetLevel(std::uint32_t a_actor, FightWithMePathsAPI::Level* a_out);

// Main thread. Returns the number written; an unknown structure size returns zero.
extern "C" FIGHTWITHMEPATHS_API std::uint32_t FightWithMePaths_GetStatuses(std::uint32_t actor, std::uint32_t size, FightWithMePathsAPI::Status* out, std::uint32_t capacity);

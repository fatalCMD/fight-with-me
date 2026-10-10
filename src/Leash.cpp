#include "FWM/Leash.h"

#include "FWM/Game.h"
#include "FWM/Log.h"

namespace FWM::Leash
{
	namespace
	{
		// CombatAreaReference, read from its 1.5.97 constructor (43261) and the
		// controller's area update (32523):
		struct Area
		{
			void*                 vtable;      // 00
			std::uint32_t         refCount;    // 08
			std::uint32_t         pad0C;       // 0C
			RE::CombatController* controller;  // 10
			const void*           owner;       // 18 - the package procedure that made it; ours is a marker
			float                 updatedAt;   // 20
			float                 interval;    // 24
			std::int32_t          priority;    // 28 - default area 1, hold position 3
			bool                  valid;       // 2C
			bool                  enabled;     // 2D
			std::uint16_t         pad2E;       // 2E
			RE::NiPoint3          center;      // 30
			std::uint32_t         pad3C;       // 3C
			void*                 space;       // 40 - worldspace outdoors, the cell indoors
			float                 radius;      // 48
			std::uint32_t         pad4C;       // 4C
			std::uint32_t         reference;   // 50 - handle the centre follows
			float                 refreshedAt; // 54
			float                 refresh;     // 58
			std::uint32_t         pad5C;       // 5C
		};
		static_assert(offsetof(Area, owner) == 0x18);
		static_assert(offsetof(Area, priority) == 0x28);
		static_assert(offsetof(Area, enabled) == 0x2D);
		static_assert(offsetof(Area, radius) == 0x48);
		static_assert(sizeof(Area) == 0x60);

		constexpr std::int32_t kPriority = 2;

		// The last radius and centre logged per companion: a new controller after a
		// bleedout rebuilds the same area, which is not news.
		std::unordered_map<RE::FormID, std::pair<float, std::uint32_t>> logged;
		const std::uint8_t     marker{ 0 };  // its address identifies our areas

		enum class Status
		{
			kUnchecked,
			kReady,
			kOff
		};

		Status         status{ Status::kUnchecked };
		bool           layoutChecked{ false };
		std::uintptr_t referenceVtable{ 0 };
		std::uintptr_t standardVtable{ 0 };
		std::int32_t*  liveObjects{ nullptr };  // the engine's count of live combat objects

		bool InImage(std::uintptr_t a_address)
		{
			const auto& module = REL::Module::get();
			return a_address >= module.base() && a_address < module.base() + 0x8000000;
		}

		// "lock dec dword [rip+disp32]" (F0 FF 0D): the destructor's decrement of the count.
		std::int32_t* FindDecrement(std::uintptr_t a_code)
		{
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_code);
			for (std::size_t i = 0; i + 7 <= 96; ++i) {
				if (bytes[i] == 0xF0 && bytes[i + 1] == 0xFF && bytes[i + 2] == 0x0D) {
					std::int32_t disp = 0;
					std::memcpy(&disp, bytes + i + 3, sizeof(disp));
					const auto target = a_code + i + 7 + static_cast<std::intptr_t>(disp);
					return InImage(target) ? reinterpret_cast<std::int32_t*>(target) : nullptr;
				}
				if (bytes[i] == 0xC3) {
					break;
				}
			}
			return nullptr;
		}

		// Every combat object's constructor counts it in and its destructor counts it out.
		// The constructor is not called here, so the count is kept by hand; its address
		// comes from the area's own destructor, which works on any version.
		std::int32_t* FindObjectCount(std::uintptr_t a_vtable)
		{
			const auto destructor = *reinterpret_cast<const std::uintptr_t*>(a_vtable);
			if (auto* count = FindDecrement(destructor)) {
				return count;
			}
			// Otherwise it calls the base class destructor. An E8 byte can also sit inside
			// another instruction, so each candidate is tried; the caller cross-checks.
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(destructor);
			for (std::size_t i = 0; i + 5 <= 64; ++i) {
				if (bytes[i] != 0xE8) {
					continue;
				}
				std::int32_t rel = 0;
				std::memcpy(&rel, bytes + i + 1, sizeof(rel));
				const auto target = destructor + i + 5 + static_cast<std::intptr_t>(rel);
				if (InImage(target)) {
					if (auto* count = FindDecrement(target)) {
						return count;
					}
				}
			}
			return nullptr;
		}

		void Check()
		{
			if (status != Status::kUnchecked) {
				return;
			}
			status = Status::kOff;
			if (REL::Module::IsVR()) {
				Log::Info("[Leash] Not available in VR; positions work through targeting alone."sv);
				return;
			}
			referenceVtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_CombatAreaReference[0] }.address();
			standardVtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_CombatAreaStandard[0] }.address();
			if (!referenceVtable || !standardVtable) {
				Log::Warn("[Leash] Combat area vtables not found; positions work through targeting alone."sv);
				return;
			}
			// Two different areas' destructors must agree on where the count lives.
			liveObjects = FindObjectCount(referenceVtable);
			if (!liveObjects || liveObjects != FindObjectCount(standardVtable)) {
				Log::Warn("[Leash] The combat object count was not found; positions work through targeting alone."sv);
				return;
			}
			status = Status::kReady;
			Log::Info("[Leash] Ready (object count at +{:X})."sv, reinterpret_cast<std::uintptr_t>(liveObjects) - REL::Module::get().base());
		}

		// Before the first area is made, the controller's own default area must sit where
		// this layout expects: same controller at +10, a small priority at +28.
		bool LayoutHolds(RE::CombatController* a_controller)
		{
			if (layoutChecked) {
				return true;
			}
			for (auto* entry : a_controller->GetRuntimeData().areas) {
				auto* area = reinterpret_cast<Area*>(entry);
				if (!area || reinterpret_cast<std::uintptr_t>(area->vtable) != standardVtable) {
					continue;
				}
				if (area->controller == a_controller && area->priority >= 0 && area->priority <= 8) {
					layoutChecked = true;
					Log::Info("[Leash] Combat area layout matches (default area priority {})."sv, area->priority);
					return true;
				}
				status = Status::kOff;
				Log::Warn("[Leash] Combat area layout differs on this version; positions work through targeting alone."sv);
				return false;
			}
			return false;  // no default area yet: ask again next time
		}

		Area* Find(RE::CombatController* a_controller)
		{
			for (auto* entry : a_controller->GetRuntimeData().areas) {
				auto* area = reinterpret_cast<Area*>(entry);
				if (area && reinterpret_cast<std::uintptr_t>(area->vtable) == referenceVtable && area->owner == &marker) {
					return area;
				}
			}
			return nullptr;
		}

		// Where the centre lives: the worldspace outdoors, the cell indoors.
		void* SpaceOf(RE::TESObjectREFR* a_reference)
		{
			auto* cell = a_reference->GetParentCell();
			if (!cell) {
				return nullptr;
			}
			if (cell->IsInteriorCell()) {
				return cell;
			}
			auto* world = cell->GetRuntimeData().worldSpace;
			return world ? static_cast<void*>(world) : static_cast<void*>(cell);
		}

		// What constructor 43261 does, field by field.
		Area* Create(RE::CombatController* a_controller, RE::TESObjectREFR* a_center, float a_radius)
		{
			auto* area = static_cast<Area*>(RE::malloc(sizeof(Area)));
			if (!area) {
				return nullptr;
			}
			std::memset(area, 0, sizeof(Area));
			area->vtable = reinterpret_cast<void*>(referenceVtable);
			std::atomic_ref<std::int32_t>(*liveObjects).fetch_add(1);
			area->controller = a_controller;
			area->owner = &marker;
			area->updatedAt = 0.0f;  // stale on purpose: the first update re-centres it
			area->interval = 0.25f;
			area->priority = kPriority;
			area->valid = true;
			area->enabled = true;
			area->center = a_center->GetPosition();
			area->space = SpaceOf(a_center);
			area->radius = a_radius;
			area->reference = a_center->GetHandle().native_handle();
			// The controller's list holds the only reference and frees the area with itself.
			a_controller->GetRuntimeData().areas.push_back(reinterpret_cast<RE::CombatAreaStandard*>(area));
			std::atomic_ref<std::uint32_t>(area->refCount).fetch_add(1);
			return area;
		}
	}

	bool Available() noexcept
	{
		Check();
		return status == Status::kReady;
	}

	void Apply(RE::Actor* a_follower, float a_radius, RE::Actor* a_center)
	{
		if (!Available() || !a_follower) {
			return;
		}
		auto*      controller = Game::Controller(a_follower);
		RE::Actor* center = a_center ? a_center : RE::PlayerCharacter::GetSingleton();
		if (!controller || !center) {
			return;
		}
		auto* area = Find(controller);
		if (a_radius <= 0.0f) {
			if (area && area->enabled) {
				area->enabled = false;
			}
			return;
		}
		if (!area) {
			if (!LayoutHolds(controller)) {
				return;
			}
			area = Create(controller, center, a_radius);
			if (!area) {
				return;
			}
		}
		const auto handle = center->GetHandle().native_handle();
		if (area->reference != handle) {
			area->reference = handle;
			area->center = center->GetPosition();
			area->space = SpaceOf(center);
			area->updatedAt = 0.0f;
		}
		area->radius = a_radius;
		if (auto& last = logged[a_follower->GetFormID()]; last != std::pair{ a_radius, handle }) {
			last = { a_radius, handle };
			Log::Info("[Leash] {} keeps within {:.0f} of {}."sv, Game::Name(a_follower), a_radius,
				center->IsPlayerRef() ? std::string("you") : Game::Name(center));
		}
		area->enabled = true;
	}
}

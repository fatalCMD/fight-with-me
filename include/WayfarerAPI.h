#pragma once

#include <cstdint>

namespace WayfarerAPI
{
    inline constexpr std::uint32_t INTERFACE_VERSION = 1;

    enum class FormationMode : std::int32_t
    {
        kDynamic = 0,
        kLead = 1,
        kCompanion = 2,
        kRear = 3,
        kSandbox = 4,
        kVanilla = 5,
        kCustom = 6 // Ignored until the player has saved a custom formation.
    };

    class IWayfarer
    {
      public:
        virtual ~IWayfarer() = default;
        [[nodiscard]] virtual std::uint32_t GetVersion() const noexcept = 0;
        virtual bool RegisterFollower(std::uint32_t a_actorFormID, std::int32_t a_preferredSlot = -1) = 0;
        virtual bool UnregisterFollower(std::uint32_t a_actorFormID) = 0;
        virtual bool ExcludeFollower(std::uint32_t a_actorFormID) = 0;
        virtual bool IncludeFollower(std::uint32_t a_actorFormID) = 0;
        [[nodiscard]] virtual bool IsManaged(std::uint32_t a_actorFormID) const = 0;
        virtual void SetEnabled(bool a_enabled) = 0;
        [[nodiscard]] virtual bool IsEnabled() const = 0;
        virtual void SetFormationMode(FormationMode a_mode) = 0;
        [[nodiscard]] virtual FormationMode GetFormationMode() const = 0;
        [[nodiscard]] virtual std::uint32_t GetManagedCount() const = 0;
    };
} // namespace WayfarerAPI

#if defined(WAYFARER_EXPORTS)
#define WAYFARER_API __declspec(dllexport)
#else
#define WAYFARER_API __declspec(dllimport)
#endif

extern "C" WAYFARER_API WayfarerAPI::IWayfarer* Wayfarer_GetInterface(std::uint32_t a_version);

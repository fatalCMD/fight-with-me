#include "FWM/WalkWithMe.h"

#include "FWM/Log.h"

#include "WayfarerAPI.h"

namespace FWM::WalkWithMe
{
	namespace
	{
		WayfarerAPI::IWayfarer* api{ nullptr };
	}

	void Connect()
	{
		auto* module = ::GetModuleHandleW(L"Wayfarer.dll");
		if (!module) {
			Log::Info("[Walk With Me] Not installed; the party is every follower in reach."sv);
			return;
		}
		using GetInterface = WayfarerAPI::IWayfarer* (*)(std::uint32_t);
		const auto get = reinterpret_cast<GetInterface>(::GetProcAddress(module, "Wayfarer_GetInterface"));
		api = get ? get(WayfarerAPI::INTERFACE_VERSION) : nullptr;
		if (api) {
			Log::Info("[Walk With Me] Connected (interface {})."sv, api->GetVersion());
		} else {
			Log::Warn("[Walk With Me] Wayfarer.dll is loaded but its interface did not answer."sv);
		}
	}

	bool Connected() noexcept
	{
		return api != nullptr;
	}

	bool IsManaged(RE::FormID a_actor)
	{
		return api && a_actor && api->IsManaged(a_actor);
	}

	Formation CurrentFormation()
	{
		if (!api || !api->IsEnabled()) {
			return Formation::kUnknown;
		}
		return static_cast<Formation>(static_cast<std::int32_t>(api->GetFormationMode()));
	}
}

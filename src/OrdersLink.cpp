#include "FWM/OrdersLink.h"

#include "FWM/Game.h"
#include "FWM/Log.h"

#include "FightWithMeAPI.h"

namespace FWM::OrdersLink
{
	namespace
	{
		FightWithMeAPI::IFightWithMe* api{ nullptr };
		std::uint32_t (*getMark)(std::uint32_t){ nullptr };
        std::int32_t (*getPosition)(std::uint32_t){ nullptr };
	}

	void Connect()
	{
		auto* module = ::GetModuleHandleW(L"FightWithMe.dll");
		if (!module) {
			return;
		}
		using GetInterface = FightWithMeAPI::IFightWithMe* (*)(std::uint32_t);
		const auto get = reinterpret_cast<GetInterface>(::GetProcAddress(module, "FightWithMe_GetInterface"));
		api = get ? get(FightWithMeAPI::INTERFACE_VERSION) : nullptr;
		getMark = reinterpret_cast<std::uint32_t (*)(std::uint32_t)>(::GetProcAddress(module, "FightWithMe_GetMark"));
        getPosition=reinterpret_cast<decltype(getPosition)>(::GetProcAddress(module,"FightWithMe_GetPosition"));
		if (!api) {
			Log::Warn("[Orders] FightWithMe.dll offers no interface this version understands; paths follow each companion's gear."sv);
			return;
		}
		Log::Info("[Orders] Fight With Me gives each companion their role; the path that counts follows it{}."sv,
			getMark ? ", and their abilities go for whoever they were sent after" : "");
	}

	bool Active()
	{
		return api != nullptr;
	}

	std::optional<Role> RoleOf(RE::FormID a_member)
	{
		if (!api) {
			return std::nullopt;
		}
		const auto role = static_cast<std::int32_t>(api->GetRole(a_member));
		if (role < 0 || role >= static_cast<std::int32_t>(Role::kCount)) {
			return std::nullopt;
		}
		return static_cast<Role>(role);
	}

	RE::ActorHandle MarkOf(RE::FormID a_member)
	{
		return getMark ? Game::FromNative(getMark(a_member)) : RE::ActorHandle{};
	}

    std::optional<Position> PositionOf(RE::FormID member)
    {
        const auto value=getPosition?getPosition(member):-1;
        if(value<0||value>=static_cast<std::int32_t>(Position::kCount))return std::nullopt;
        return Live(static_cast<Position>(value));
    }
}

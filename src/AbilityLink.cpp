#include "FWM/AbilityLink.h"

#include "FWM/Game.h"
#include "FWM/Host.h"
#include "FWM/LoadoutPolicy.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Paths.h"
#include "FWM/Text.h"

#include "FightWithMePathsAPI.h"

namespace FWM::AbilityLink
{
	namespace
	{
		FightWithMePathsAPI::IPaths* api{ nullptr };
		bool (*isDecoy)(RE::FormID){nullptr};
		std::uint32_t (*loadoutMask)(std::uint32_t){ nullptr };
		bool                         mismatched{ false };  // said once, at the first update

		std::unordered_map<RE::FormID, Request> requests;
		std::vector<Hold>                       holds;

		std::vector<FightWithMePathsAPI::Steer> steerBuffer;
		std::vector<FightWithMePathsAPI::Hold>  holdBuffer;

		RE::ObjectRefHandle RefOf(std::uint32_t a_native)
		{
			RE::ObjectRefHandle handle;
			static_assert(sizeof(handle) == sizeof(a_native));
			std::memcpy(&handle, &a_native, sizeof(a_native));
			return handle;
		}
	}

	void Connect()
	{
		auto* module = ::GetModuleHandleW(L"FightWithMePaths.dll");
		if (!module) {
			Log::Info("[Paths] Path of the Companions is not installed: battle orders only, no abilities."sv);
			return;
		}
		using GetInterface = FightWithMePathsAPI::IPaths* (*)(std::uint32_t);
		const auto get = reinterpret_cast<GetInterface>(::GetProcAddress(module, "FightWithMePaths_GetInterface"));
		auto*      found = get ? get(FightWithMePathsAPI::INTERFACE_VERSION) : nullptr;
		if (!found || found->GetTreeSignature() != Paths::Signature()) {
			Log::Warn("[Paths] FightWithMePaths.dll is from another version of Fight With Me; update both. Its abilities still run, but Fight With Me ignores what they ask of a fight."sv);
			mismatched = true;
			return;
		}
		api = found;
		loadoutMask = reinterpret_cast<decltype(loadoutMask)>(::GetProcAddress(module, "FightWithMePaths_GetLoadoutMask"));
		if (loadoutMask) {
			Log::Info("[Paths] Role restrictions connected for weapon and spell pools."sv);
		} else {
			Log::Warn("[Paths] This Paths DLL has no loadout policy; update Paths to enable role restrictions in pools."sv);
		}
		isDecoy = reinterpret_cast<decltype(isDecoy)>(::GetProcAddress(module,"FightWithMePaths_IsWindstepDecoy"));
		Log::Info("[Paths] Path of the Companions runs the abilities; Fight With Me carries out what they ask of a fight."sv);
	}

	bool Active()
	{
		return api != nullptr;
	}
	std::optional<std::uint32_t> LoadoutMask(Role role)
	{
		// Shared pool code also runs inside Paths, where the policy is owned locally.
		if (Host::Abilities()) return LoadoutPolicy::RoleWeapons(role);
		if (!api || !loadoutMask) return std::nullopt;
		return loadoutMask(static_cast<std::uint32_t>(role));
	}

	bool IsWindstepDecoy(RE::FormID actor) { return isDecoy && isDecoy(actor); }

	void Fetch()
	{
		if (mismatched) {
			mismatched = false;
			Notices::Push(Tr("Path of the Companions does not match this version of Fight With Me. Update both."), Notices::Kind::kWarning);
		}
		requests.clear();
		holds.clear();
		if (!api) {
			return;
		}
		steerBuffer.resize(64);
		const auto steered = api->Steering(steerBuffer.data(), static_cast<std::uint32_t>(steerBuffer.size()));
		for (std::uint32_t i = 0; i < steered && i < steerBuffer.size(); ++i) {
			const auto& in = steerBuffer[i];
			Request     out;
			out.target = Game::FromNative(in.target);
			out.circle = RefOf(in.circle);
			out.radius = in.radius;
			out.tweaks.bash = in.bash;
			out.tweaks.power = in.power;
			out.tweaks.flank = in.flank;
			out.tweaks.circle = in.circling;
			out.tweaks.fallback = in.fallback;
			out.tweaks.strafe = in.strafe;
			out.tweaks.avoidThreat = in.avoidThreat;
			out.tweaks.offensive = in.offensive;
			out.tweaks.defensive = in.defensive;
			requests[in.actor] = out;
		}
		holdBuffer.resize(64);
		const auto held = api->Holds(holdBuffer.data(), static_cast<std::uint32_t>(holdBuffer.size()));
		for (std::uint32_t i = 0; i < held && i < holdBuffer.size(); ++i) {
			holds.push_back({ Game::FromNative(holdBuffer[i].enemy), Game::FromNative(holdBuffer[i].tank) });
		}
	}

	const Request* For(RE::FormID a_member)
	{
		const auto it = requests.find(a_member);
		return it != requests.end() ? &it->second : nullptr;
	}

	const std::vector<Hold>& Holds()
	{
		return holds;
	}

	bool Adopt(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, const std::vector<std::uint8_t>& a_bytes)
	{
		if (!api) {
			return false;
		}
		api->Adopt(a_intfc, a_type, a_version, a_bytes.data(), static_cast<std::uint32_t>(a_bytes.size()));
		return true;
	}

	void Clear()
	{
		requests.clear();
		holds.clear();
	}

	void DrawInPlan(FightWithMePathsAPI::Plan& a_plan)
	{
		if (api) {
			api->DrawInPlan(&a_plan);
		}
	}
}

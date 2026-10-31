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
        bool (*isPerforming)(RE::FormID){nullptr};
        bool (*isSkaldRelocating)(RE::FormID){nullptr};
		std::uint32_t (*loadoutMask)(std::uint32_t){ nullptr };
		bool (*levelOf)(std::uint32_t, FightWithMePathsAPI::Level*){ nullptr };
		std::uint32_t (*statusesOf)(std::uint32_t, std::uint32_t, FightWithMePathsAPI::Status*, std::uint32_t){ nullptr };
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
		isPerforming = reinterpret_cast<decltype(isPerforming)>(::GetProcAddress(module,"FightWithMePaths_IsPerforming"));
        isSkaldRelocating=reinterpret_cast<decltype(isSkaldRelocating)>(::GetProcAddress(module,"FightWithMePaths_IsSkaldRelocating"));
        isDecoy = reinterpret_cast<decltype(isDecoy)>(::GetProcAddress(module,"FightWithMePaths_IsWindstepDecoy"));
		levelOf = reinterpret_cast<decltype(levelOf)>(::GetProcAddress(module, "FightWithMePaths_GetLevel"));
		statusesOf = reinterpret_cast<decltype(statusesOf)>(::GetProcAddress(module, "FightWithMePaths_GetStatuses"));
		if (!levelOf) {
			Log::Warn("[Paths] This Paths DLL does not share battle levels; update Paths to see their XP in the battle plan."sv);
		}
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

	bool Performing(RE::FormID actor) {return isPerforming && isPerforming(actor);}
    bool SkaldRelocating(RE::FormID actor){return isSkaldRelocating&&isSkaldRelocating(actor);}
    bool SkaldUnlocked(RE::FormID actor) {
        FightWithMePathsAPI::Record record{};
        return api && api->Read(actor,&record) && (record.flags & FightWithMePathsAPI::kSkaldUnlockedFlag)!=0;
    }

    bool IsWindstepDecoy(RE::FormID actor) { return isDecoy && isDecoy(actor); }

    void SkaldProgress(View::Member& member) {
        FightWithMePathsAPI::Record record{};
        if(!api || !api->Read(member.id,&record))return;
        member.record.skaldUnlocked=(record.flags & FightWithMePathsAPI::kSkaldUnlockedFlag)!=0;
        member.unlimited=(record.flags & FightWithMePathsAPI::kUnlimitedFlag)!=0;
        member.pointsEarned=record.earned;
        member.pointsSpent=record.spent;
    }

    Paths::Block UnlockSkald(RE::FormID actor) {
        return api ? static_cast<Paths::Block>(api->Learn(actor,static_cast<std::uint32_t>(Role::kSkald),Paths::kNodes)) : Paths::Block::kBadNode;
    }

    void Statuses(RE::FormID actor, std::vector<View::Status>& out) {
        if(!api || !statusesOf)return;
        std::array<FightWithMePathsAPI::Status,32> rows{};
        const auto count=std::min(statusesOf(actor,sizeof(rows[0]),rows.data(),static_cast<std::uint32_t>(rows.size())),static_cast<std::uint32_t>(rows.size()));
        for(std::uint32_t i=0;i<count;++i){
            const auto& row=rows[i];
            if(row.tint>static_cast<std::uint32_t>(View::Status::Tint::kMagicka) || !std::isfinite(row.left) || !std::isfinite(row.length) || !std::isfinite(row.progress))continue;
            out.push_back({std::string(row.name,std::find(std::begin(row.name),std::end(row.name),'\0')),
                std::string(row.detail,std::find(std::begin(row.detail),std::end(row.detail),'\0')),
                row.glyph,static_cast<View::Status::Tint>(row.tint),row.left,row.length,row.progress});
        }
    }

	std::optional<Leveling::Progress> Level(RE::FormID a_member)
	{
		FightWithMePathsAPI::Level level{};
		if (!api || !levelOf || !levelOf(a_member, &level)) {
			return std::nullopt;
		}
		return Leveling::Progress{ level.level, level.xp, level.next, level.sinceLevelUp, level.capped };
	}

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

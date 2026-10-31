#include "FWM/PathsLink.h"
#include "FWM/DebugAbility.h"

#include "FWM/Host.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"

#include "FightWithMePathsAPI.h"

namespace FWM::PathsLink
{
	namespace
	{
		RE::FormID debugID = 0;
		const Kit* debugKit = nullptr;
		FightWithMePathsAPI::IPaths* api{ nullptr };
		bool                         mismatched{ false };  // the add-on is here but built for other trees: said once, after a load

		struct Copy
		{
			Progress      progress;
			std::uint64_t at{ 0 };
		};
		std::unordered_map<RE::FormID, Copy> copies;
		constexpr std::uint64_t              kStaleMs = 1000;

		// Unspent points each companion was last told about, kept across loads so loading a
		// save does not repeat the news.
		std::unordered_map<RE::FormID, int> told;

		Paths::Record ToRecord(const FightWithMePathsAPI::Record& a_in)
		{
			Paths::Record out;
			for (std::size_t r = 0; r < Paths::kRoles; ++r) {
				for (std::size_t i = 0; i < Paths::kNodes; ++i) {
					out.ranks[r][i] = a_in.ranks[r][i];
				}
				for (std::size_t s = 0; s < Paths::kSlots; ++s) {
					const auto ability = a_in.slots[r][s];
					out.slots[r][s] = ability < Paths::kAbilityCount ? static_cast<Paths::Ability>(ability) : Paths::Ability::kNone;
				}
				out.bonus[r] = a_in.bonus[r];
			}
			out.peak = a_in.peak;
            out.skaldUnlocked=(a_in.flags & FightWithMePathsAPI::kSkaldUnlockedFlag)!=0;
			return out;
		}

		Copy* Read(RE::FormID a_id)
		{
			if (!api || !a_id || !Party::CanHaveRole(a_id)) {
				return nullptr;
			}
			FightWithMePathsAPI::Record raw{};
			if (!api->Read(a_id, &raw)) {
				return nullptr;
			}
			auto& copy = copies[a_id];
			copy.progress.record = ToRecord(raw);
			copy.progress.earned = raw.earned;
			copy.progress.spent = raw.spent;
			copy.progress.unlimited = (raw.flags & FightWithMePathsAPI::kUnlimitedFlag) != 0;
			copy.at = ::GetTickCount64();
			return &copy;
		}

		std::string NameOf(RE::FormID a_id)
		{
			if (const auto* member = Party::Find(a_id)) {
				return member->name;
			}
			auto*       actor = RE::TESForm::LookupByID<RE::Actor>(a_id);
			const char* name = actor ? actor->GetDisplayFullName() : nullptr;
			return name && *name ? std::string(name) : std::string(Tr("A companion"));
		}

		Outcome Answered(int a_result, std::string a_done)
		{
			const auto block = static_cast<Paths::Block>(a_result);
			if (block == Paths::Block::kNone) {
				return { true, std::move(a_done) };
			}
			return { false, std::string(Paths::Why(block)) };
		}
	}

	void Connect()
	{
		auto* module = ::GetModuleHandleW(L"FightWithMePaths.dll");
		if (!module) {
			Log::Info("[Paths] The Paths add-on is not installed; every role has its own abilities, growing with level."sv);
			return;
		}
		using GetInterface = FightWithMePathsAPI::IPaths* (*)(std::uint32_t);
		const auto get = reinterpret_cast<GetInterface>(::GetProcAddress(module, "FightWithMePaths_GetInterface"));
		auto*      found = get ? get(FightWithMePathsAPI::INTERFACE_VERSION) : nullptr;
		if (!found) {
			Log::Warn("[Paths] FightWithMePaths.dll is loaded but offers no interface this version understands; it is not used."sv);
			return;
		}
		if (found->GetTreeSignature() != Paths::Signature()) {
			Log::Warn("[Paths] FightWithMePaths.dll was built for different trees ({:08X}, these are {:08X}); update both so they match. Not used."sv,
				found->GetTreeSignature(), Paths::Signature());
			mismatched = true;
			return;
		}
		api = found;
		Log::Info("[Paths] The paths are open: a point for every level past {}, giving points back {}."sv, api->PointsAboveLevel(),
			api->RespecAllowed() ? "allowed" : "off");
	}

	bool Active()
	{
		return api != nullptr;
	}

	bool ClassicCarries(Role a_role, Paths::Ability a_ability)
	{
		using A = Paths::Ability;
		switch (a_role) {
		case Role::kTank:
			return a_ability == A::kTaunt || a_ability == A::kBodyguard;
		case Role::kHealer:
			return a_ability == A::kHeal || a_ability == A::kRejuvenate || a_ability == A::kWard;
		case Role::kBruiser:
			return a_ability == A::kWarCry;
		case Role::kFighter:
			return a_ability == A::kDisarm;
		case Role::kArcher:
			return a_ability == A::kVolley;
		case Role::kMage:
			return a_ability == A::kWard;
		default:
			return false;
		}
	}

	bool Kit::Carries(Paths::Ability a_ability) const
	{
		if (a_ability == Paths::Ability::kNone) {
			return false;
		}
		return tree ? std::find(slots.begin(), slots.end(), a_ability) != slots.end() : ClassicCarries(role, a_ability);
	}

	int Kit::Rank(Paths::Perk a_perk) const
	{
		return tree && record ? Paths::Rank(*record, role, a_perk) : 0;
	}

	int Kit::Rank(Paths::Ability a_ability) const
	{
		return tree && record ? Paths::Rank(*record, role, a_ability) : 0;
	}

	float Kit::Value(Paths::Perk a_perk) const
	{
		return Paths::Value(a_perk, Rank(a_perk));
	}

	float Kit::Second(Paths::Perk a_perk) const
	{
		return Paths::Second(a_perk, Rank(a_perk));
	}

	float Kit::Cooldown(Paths::Ability a_ability) const
	{
		return tree && record ? Paths::Cooldown(*record, role, a_ability) : 0.0f;
	}

	bool AutoOn(const Orders& a_orders, const Kit& a_kit, Paths::Ability a_ability)
	{
		if (a_kit.tree) {
			return (a_orders.manual & Bit(a_ability)) == 0;
		}
		switch (a_ability) {
		case Paths::Ability::kTaunt:
		case Paths::Ability::kHeal:
			return a_orders.autoAbility;
		case Paths::Ability::kRejuvenate:
			return a_orders.autoRejuvenate;
		default:
			return a_orders.autoSpecial;
		}
	}

	ScopedDebugCast::ScopedDebugCast(RE::FormID id, const Kit& kit) : previousID(debugID), previousKit(debugKit) { debugID=id; debugKit=&kit; }
	ScopedDebugCast::~ScopedDebugCast() { debugID=previousID; debugKit=previousKit; }
	RE::NiPoint3 DebugAim(RE::Actor* actor) {
		const auto p=actor->GetPosition(); const auto aim=DebugAbility::Aim(p.x,p.y,p.z,actor->GetAngleZ());
		return {aim[0],aim[1],aim[2]};
	}
	bool DebugCasting(RE::FormID id) { return debugKit && debugID==id; }
	Kit KitOf(RE::FormID a_id, Role a_role)
	{
		if (DebugCasting(a_id)) return *debugKit;
		Kit kit;
		kit.role = a_role;
        const auto* member=Party::Find(a_id);
        const bool bruiserMissing=a_role==Role::kBruiser && (member && member->arsenalKnown ? !member->arsenal.twoHanded : Party::LackFor(a_id,a_role)!=Lack::kNothing);
        if (!Party::CanHaveRole(a_id) || bruiserMissing) { kit.tree = true; return kit; }
		if (!api) {
			return kit;
		}
		const auto* progress = Find(a_id);
		if (!progress) {
			return kit;
		}
		kit.tree = true;
		kit.record = &progress->record;
		kit.slots = progress->record.slots[static_cast<std::size_t>(a_role)];
		return kit;
	}

	const Progress* Find(RE::FormID a_id)
	{
		if (!api || !Party::CanHaveRole(a_id)) {
			return nullptr;
		}
		const auto it = copies.find(a_id);
		if (it != copies.end() && ::GetTickCount64() - it->second.at < kStaleMs) {
			return &it->second.progress;
		}
		const auto* copy = Read(a_id);
		return copy ? &copy->progress : nullptr;
	}

	const Progress* ReadNow(RE::FormID a_id)
	{
		const auto* copy = Read(a_id);
		return copy ? &copy->progress : nullptr;
	}

	void Refresh()
	{
		if (mismatched) {
			mismatched = false;
			Notices::Push(Tr("Path of the Companions does not match this version of Fight With Me. Update both."), Notices::Kind::kWarning);
		}
		if (!api) {
			return;
		}
		std::vector<std::pair<std::string, int>> news;
		for (const auto& member : Party::Members()) {
			if (!member.CanTakeRole()) {
				continue;
			}
			auto* copy = Read(member.id);
			if (!copy) {
				continue;
			}
			const int  unspent = copy->progress.Unspent();
			const auto it = told.find(member.id);
			if ((it == told.end() || unspent > it->second) && unspent > 0 && !copy->progress.unlimited) {
				news.emplace_back(member.name, unspent);
			}
			told[member.id] = unspent;
		}
		if (news.empty()) {
			return;
		}
		if (news.size() == 1) {
			const auto& [name, points] = news.front();
			Notices::Push(Trf("{} has {} to spend in their skill tree.", name, Trn(points, "{} perk point", "{} perk points")), Notices::Kind::kOrder);
		} else {
			Notices::Push(Trf("{} companions have perk points to spend in their skill trees.", news.size()), Notices::Kind::kOrder);
		}
		for (const auto& [name, points] : news) {
			Log::Info("[Paths] {} has {} perk point(s) to spend."sv, name, points);
		}
	}

	Outcome Learn(RE::FormID a_id, Role a_role, std::size_t a_node)
	{
		if (static_cast<std::size_t>(a_role)>=Paths::kRoles || (a_node>=Paths::kNodes && !(a_role==Role::kSkald && a_node==Paths::kNodes))) return {false, std::string(Tr("Invalid perk."))};
        if (!Unlimited(a_id) && a_role == Role::kBruiser && Party::LackFor(a_id, a_role) != Lack::kNothing) return {false, std::string(Tr("Requires a two-handed melee weapon in inventory."))};
        if (!Party::CanHaveRole(a_id)) return { false, std::string(Tr("Roles are unavailable for creature companions and summons.")) };
		if (!api) {
			return { false, std::string(Tr("Path of the Companions is not installed.")) };
		}
        if(a_role==Role::kSkald && a_node==Paths::kNodes){
            const bool debug=Unlimited(a_id);
            const auto result=api->Learn(a_id,static_cast<std::uint32_t>(a_role),static_cast<std::uint32_t>(a_node));
            copies.erase(a_id);
            return Answered(result,std::string(debug ? Tr("Skald unlocked for Debug Mode. Disabling debug restores the previous unlock.") : Tr("Skald unlocked. 10 skill points invested.")));
        }
		const auto& node = Paths::PathOf(a_role)[std::min(a_node, Paths::kNodes - 1)];
		const auto  result = api->Learn(a_id, static_cast<std::uint32_t>(a_role), static_cast<std::uint32_t>(a_node));
		auto*       copy = Read(a_id);
		const int   rank = copy ? copy->progress.record.ranks[static_cast<std::size_t>(a_role)][a_node] : 0;
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
        if(result!=static_cast<int>(Paths::Block::kNone))return {false,Paths::Requirement(static_cast<Paths::Block>(result),node,rank+1)};
		const bool first = node.kind == Paths::Kind::kAbility && rank == 1;
		return Answered(result, first ? Trf("{} learns {}.", NameOf(a_id), Tr(node.name)) :
		                                Trf("{}: {} rank {}.", NameOf(a_id), Tr(node.name), rank));
	}

	Outcome Forget(RE::FormID a_id, Role a_role, std::size_t a_node)
	{
        if (!Party::CanHaveRole(a_id)) return { false, std::string(Tr("Roles are unavailable for creature companions and summons.")) };
		if (!api) {
			return { false, std::string(Tr("Path of the Companions is not installed.")) };
		}
		const auto& node = Paths::PathOf(a_role)[std::min(a_node, Paths::kNodes - 1)];
		const auto  result = api->Forget(a_id, static_cast<std::uint32_t>(a_role), static_cast<std::uint32_t>(a_node));
		auto*       copy = Read(a_id);
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, Trf("{} gives back a rank of {}.", NameOf(a_id), Tr(node.name)));
	}

	Outcome Reset(RE::FormID a_id, Role a_role)
	{
        if (!Party::CanHaveRole(a_id)) return { false, std::string(Tr("Roles are unavailable for creature companions and summons.")) };
		if (!api) {
			return { false, std::string(Tr("Path of the Companions is not installed.")) };
		}
		const int before = Find(a_id) ? Paths::Spent(Find(a_id)->record, a_role) : 0;
		const auto result = api->Reset(a_id, static_cast<std::uint32_t>(a_role));
		auto*      copy = Read(a_id);
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, Trf("{} starts the {} over: {} back.", NameOf(a_id), Tr(Paths::Info(a_role).title), Trn(before, "{} point", "{} points")));
	}

	Outcome Equip(RE::FormID a_id, Role a_role, std::size_t a_slot, Paths::Ability a_ability)
	{
        if (!Unlimited(a_id) && a_role == Role::kBruiser && Party::LackFor(a_id, a_role) != Lack::kNothing) return {false, std::string(Tr("Requires a two-handed melee weapon in inventory."))};
        if (!Party::CanHaveRole(a_id)) return { false, std::string(Tr("Roles are unavailable for creature companions and summons.")) };
		if (!api) {
			return { false, std::string(Tr("Path of the Companions is not installed.")) };
		}
		const auto result = api->Equip(a_id, static_cast<std::uint32_t>(a_role), static_cast<std::uint32_t>(a_slot), static_cast<std::uint32_t>(a_ability));
		Read(a_id);
		return Answered(result, Trf("{} carries {} into a fight.", NameOf(a_id), Tr(Paths::Info(a_ability).name)));
	}

	Outcome Invest(RE::FormID a_id, Role a_role, int a_room)
	{
        if (!Unlimited(a_id) && a_role == Role::kBruiser && Party::LackFor(a_id, a_role) != Lack::kNothing) return {false, std::string(Tr("Requires a two-handed melee weapon in inventory."))};
        if (!Party::CanHaveRole(a_id)) return { false, std::string(Tr("Roles are unavailable for creature companions and summons.")) };
		if (!api) {
			return { false, std::string(Tr("Path of the Companions is not installed.")) };
		}
		const auto result = api->Invest(a_id, static_cast<std::uint32_t>(a_role), a_room);
		auto*      copy = Read(a_id);
		const int  in = copy ? Paths::Invested(copy->progress.record, a_role) : 0;
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, Trf("{}: a point into the {}'s bonus ({} there).", NameOf(a_id), Tr(Paths::Info(a_role).title), in));
	}

	Outcome Divest(RE::FormID a_id, Role a_role)
	{
        if (!Party::CanHaveRole(a_id)) return { false, std::string(Tr("Roles are unavailable for creature companions and summons.")) };
		if (!api) {
			return { false, std::string(Tr("Path of the Companions is not installed.")) };
		}
		const auto result = api->Divest(a_id, static_cast<std::uint32_t>(a_role));
		auto*      copy = Read(a_id);
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, Trf("{} takes a point back out of the {}'s bonus.", NameOf(a_id), Tr(Paths::Info(a_role).title)));
	}

	Outcome SetUnlimited(RE::FormID a_id, bool a_on)
	{
        if (!Party::CanHaveRole(a_id)) return { false, std::string(Tr("Roles are unavailable for creature companions and summons.")) };
		if (!api) {
			return { false, std::string(Tr("Path of the Companions is not installed.")) };
		}
		const auto result = api->SetUnlimited(a_id, a_on);
		auto*      copy = Read(a_id);
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, a_on ? Trf("Debug Mode enabled for {}.", NameOf(a_id)) :
		                               Trf("Debug Mode disabled for {}.", NameOf(a_id)));
	}

	bool Unlimited(RE::FormID a_id)
	{
		const auto* progress = Find(a_id);
		return progress && progress->unlimited;
	}

	bool UnlimitedAllowed()
	{
		return api && api->UnlimitedAllowed();
	}

	bool RespecAllowed()
	{
		return api && api->RespecAllowed();
	}

	int PointsAboveLevel()
	{
		return api ? api->PointsAboveLevel() : 10;
	}

	void Clear()
	{
		copies.clear();
	}
}

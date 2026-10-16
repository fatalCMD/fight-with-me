#include "FWM/PathsLink.h"

#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/Settings.h"

#include "FightWithMePathsAPI.h"

namespace FWM::PathsLink
{
	namespace
	{
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
			return out;
		}

		Copy* Read(RE::FormID a_id)
		{
			if (!api || !a_id) {
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
			return name && *name ? std::string(name) : "A companion"s;
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
		Log::Info("[Paths] Connected to the Paths add-on: a point for every level past {}, giving points back {}."sv, api->PointsAboveLevel(),
			api->RespecAllowed() ? "allowed" : "off");
		// The paths guide the roles now: the Tanks and Healers settings stop being the player's to change.
		Settings::LockRoles(true);
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

	Kit KitOf(RE::FormID a_id, Role a_role)
	{
		Kit kit;
		kit.role = a_role;
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
		if (!api) {
			return nullptr;
		}
		const auto it = copies.find(a_id);
		if (it != copies.end() && ::GetTickCount64() - it->second.at < kStaleMs) {
			return &it->second.progress;
		}
		const auto* copy = Read(a_id);
		return copy ? &copy->progress : nullptr;
	}

	void Refresh()
	{
		if (mismatched) {
			mismatched = false;
			Notices::Push("Paths - Follower Ability Skilltree does not match this version of Fight With Me. Update both.", Notices::Kind::kWarning);
		}
		if (!api) {
			return;
		}
		std::vector<std::pair<std::string, int>> news;
		for (const auto& member : Party::Members()) {
			if (member.summon) {
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
			Notices::Push(std::format("{} has {} perk point{} to spend: Battle plan, Path.", name, points, points == 1 ? "" : "s"), Notices::Kind::kOrder);
		} else {
			Notices::Push(std::format("{} companions have perk points to spend: Battle plan, Path.", news.size()), Notices::Kind::kOrder);
		}
		for (const auto& [name, points] : news) {
			Log::Info("[Paths] {} has {} perk point(s) to spend."sv, name, points);
		}
	}

	Outcome Learn(RE::FormID a_id, Role a_role, std::size_t a_node)
	{
		if (!api) {
			return { false, "Paths - Follower Ability Skilltree is not installed." };
		}
		const auto& node = Paths::PathOf(a_role)[std::min(a_node, Paths::kNodes - 1)];
		const auto  result = api->Learn(a_id, static_cast<std::uint32_t>(a_role), static_cast<std::uint32_t>(a_node));
		auto*       copy = Read(a_id);
		const int   rank = copy ? copy->progress.record.ranks[static_cast<std::size_t>(a_role)][a_node] : 0;
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		const bool first = node.kind == Paths::Kind::kAbility && rank == 1;
		return Answered(result, first ? std::format("{} learns {}.", NameOf(a_id), node.name) :
		                                std::format("{}: {} rank {}.", NameOf(a_id), node.name, rank));
	}

	Outcome Forget(RE::FormID a_id, Role a_role, std::size_t a_node)
	{
		if (!api) {
			return { false, "Paths - Follower Ability Skilltree is not installed." };
		}
		const auto& node = Paths::PathOf(a_role)[std::min(a_node, Paths::kNodes - 1)];
		const auto  result = api->Forget(a_id, static_cast<std::uint32_t>(a_role), static_cast<std::uint32_t>(a_node));
		auto*       copy = Read(a_id);
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, std::format("{} gives back a rank of {}.", NameOf(a_id), node.name));
	}

	Outcome Reset(RE::FormID a_id, Role a_role)
	{
		if (!api) {
			return { false, "Paths - Follower Ability Skilltree is not installed." };
		}
		const int before = Find(a_id) ? Paths::Spent(Find(a_id)->record, a_role) : 0;
		const auto result = api->Reset(a_id, static_cast<std::uint32_t>(a_role));
		auto*      copy = Read(a_id);
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, std::format("{} starts the {} over: {} point{} back.", NameOf(a_id), Paths::Info(a_role).title, before, before == 1 ? "" : "s"));
	}

	Outcome Equip(RE::FormID a_id, Role a_role, std::size_t a_slot, Paths::Ability a_ability)
	{
		if (!api) {
			return { false, "Paths - Follower Ability Skilltree is not installed." };
		}
		const auto result = api->Equip(a_id, static_cast<std::uint32_t>(a_role), static_cast<std::uint32_t>(a_slot), static_cast<std::uint32_t>(a_ability));
		Read(a_id);
		return Answered(result, std::format("{} carries {} into a fight.", NameOf(a_id), Paths::Info(a_ability).name));
	}

	Outcome Invest(RE::FormID a_id, Role a_role, int a_room)
	{
		if (!api) {
			return { false, "Paths - Follower Ability Skilltree is not installed." };
		}
		const auto result = api->Invest(a_id, static_cast<std::uint32_t>(a_role), a_room);
		auto*      copy = Read(a_id);
		const int  in = copy ? Paths::Invested(copy->progress.record, a_role) : 0;
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, std::format("{}: a point into the {}'s bonus ({} there).", NameOf(a_id), Paths::Info(a_role).title, in));
	}

	Outcome Divest(RE::FormID a_id, Role a_role)
	{
		if (!api) {
			return { false, "Paths - Follower Ability Skilltree is not installed." };
		}
		const auto result = api->Divest(a_id, static_cast<std::uint32_t>(a_role));
		auto*      copy = Read(a_id);
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, std::format("{} takes a point back out of the {}'s bonus.", NameOf(a_id), Paths::Info(a_role).title));
	}

	Outcome SetUnlimited(RE::FormID a_id, bool a_on)
	{
		if (!api) {
			return { false, "Paths - Follower Ability Skilltree is not installed." };
		}
		const auto result = api->SetUnlimited(a_id, a_on);
		auto*      copy = Read(a_id);
		if (copy) {
			told[a_id] = copy->progress.Unspent();
		}
		return Answered(result, a_on ? std::format("{} has unlimited points. Cheater.", NameOf(a_id)) :
		                               std::format("{} is back to the points they earned.", NameOf(a_id)));
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

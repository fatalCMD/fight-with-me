#include "FWM/Sight.h"

#include "FWM/Enemies.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"
#include "FWM/WalkWithMe.h"

namespace FWM::Sight
{
	namespace
	{
		constexpr float       kLookEvery = 0.5f;
		constexpr float       kSheatheAfter = 5.0f;  // seconds with no enemy in view
		constexpr float       kLostAfter = 2.0f;     // a glimpse between trees is one sighting, not one per look
		constexpr float       kEngageEvery = 3.0f;
		constexpr int         kEngageTries = 3;      // per companion and enemy
		constexpr float       kForgetAfter = 10.0f;  // out of the question this long, an enemy can be asked about afresh
		constexpr std::size_t kNearest = 8;          // enemies a looker checks, nearest first
		constexpr float       kLeadSlack = 1500.0f;  // companions out ahead of you see further on
		constexpr float       kTellOutAfter = 1.5f;  // a companion still out of a fight this long has the reason logged
		constexpr float       kKeptAfter = 5.0f;     // in a fight this long, it held: their asks start over
		// DGIntimidateFaction, Skyrim.esm: the brawl quest puts its opponent, their friend and
		// the player in it for the length of a brawl, and only then.
		constexpr RE::FormID kBrawlFaction = 0x04CFA6;

		float lookIn{ 0.0f };
		float pending{ 0.0f };
		float clearFor{ 0.0f };
		bool  sighted{ false };  // an enemy has been in view lately
		int   undone{ 0 };       // this sighting: weapons we drew that the game put away again

		struct Watch
		{
			RE::FormID id{ 0 };
			float      engageIn{ 0.0f };
			bool       drew{ false };   // their weapons are out because of us
			bool       tried{ false };  // drawn once this sighting; the game may put them away, and that stands
			float       outFor{ 0.0f };   // in a fight, seconds they have been out of it
			std::string held;             // why they were last left out, as logged
			float       bareFor{ 0.0f };  // in combat without a combat controller
			bool        bareTold{ false };
			float       inFor{ 0.0f };    // seconds in combat without a break
		};
		std::vector<Watch> watches;

		// Who was set on whom. It outlives sightings and fights: a fight that falls through at
		// once, or an enemy flickering in and out of view, would otherwise have the same
		// companion set on the same enemy, and announced, every few seconds.
		struct Asked
		{
			RE::FormID      member{ 0 };
			RE::ActorHandle enemy;
			int             tries{ 0 };
			float           idle{ 0.0f };  // seconds since this companion last had reason to take them on
			bool            told{ false };
		};
		std::vector<Asked> asked;

		Watch& WatchOf(RE::FormID a_id)
		{
			for (auto& watch : watches) {
				if (watch.id == a_id) {
					return watch;
				}
			}
			return watches.emplace_back(Watch{ a_id });
		}

		Asked& AskedOf(RE::FormID a_member, const RE::ActorHandle& a_enemy)
		{
			for (auto& ask : asked) {
				if (ask.member == a_member && ask.enemy == a_enemy) {
					return ask;
				}
			}
			return asked.emplace_back(Asked{ a_member, a_enemy });
		}

		// Twice a second, with the time since the last look.
		bool Due(float a_dt, float& a_elapsed)
		{
			pending += a_dt;
			lookIn -= a_dt;
			if (lookIn > 0.0f) {
				return false;
			}
			lookIn = kLookEvery;
			a_elapsed = pending;
			pending = 0.0f;
			for (auto& watch : watches) {
				watch.engageIn = std::max(watch.engageIn - a_elapsed, 0.0f);
			}
			std::erase_if(watches, [](const Watch& a_watch) { return !Party::Find(a_watch.id); });
			for (auto& ask : asked) {
				ask.idle += a_elapsed;
			}
			std::erase_if(asked, [](const Asked& a_ask) {
				auto enemy = a_ask.enemy.get();
				return a_ask.idle > kForgetAfter || !enemy || enemy->IsDead() || !Party::Find(a_ask.member);
			});
			return true;
		}

		bool Paused()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && (ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME));
		}

		// Someone a companion could fight at all: alive, up, not one of us, and not the one
		// you are brawling with (companions stay out of a brawl, as in the game).
		bool Opponent(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
		{
			if (!Game::Usable(a_actor) || a_actor == a_player || a_actor->IsPlayerTeammate() || Party::IsPartyActor(a_actor)) {
				return false;
			}
			const auto* state = a_actor->AsActorState();
			if (state->IsBleedingOut() || state->IsUnconscious()) {
				return false;
			}
			if (a_actor->IsCommandedActor()) {
				if (auto boss = a_actor->GetCommandingActor(); boss && (boss.get() == a_player || Party::IsPartyActor(boss.get()))) {
					return false;
				}
			}
			static auto* brawl = RE::TESForm::LookupByID<RE::TESFaction>(kBrawlFaction);
			return !brawl || !a_actor->IsInFaction(brawl);
		}

		std::vector<RE::FormID> leftAlone;  // logged once each

		// A fox or an elk is hostile by the game's test, but unaggressive wildlife comes at
		// nobody until a fight finds it, so it brings no weapons out and starts no fight.
		bool KeepsToItself(RE::Actor* a_actor)
		{
			const auto* race = a_actor->GetRace();
			const bool  wildlife = race && !race->HasKeywordString("ActorTypeNPC");
			const float aggression = a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression);
			if (!Rules::KeepsToItself(wildlife, aggression, a_actor->IsInCombat())) {
				return false;
			}
			if (std::find(leftAlone.begin(), leftAlone.end(), a_actor->GetFormID()) == leftAlone.end()) {
				leftAlone.push_back(a_actor->GetFormID());
				Log::Info("[Sight] {} keeps to itself (aggression {:.0f}, in no fight); not an enemy until it is."sv, Game::Name(a_actor), aggression);
			}
			return true;
		}

		bool Enemy(RE::Actor* a_actor, RE::PlayerCharacter* a_player)
		{
			return Opponent(a_actor, a_player) && a_actor->IsHostileToActor(a_player) && !KeepsToItself(a_actor);
		}

		// On their feet and free: not seated, mounted, swimming, down or already fighting.
		bool Free(RE::Actor* a_actor)
		{
			const auto* state = a_actor->AsActorState();
			return Game::Usable(a_actor) && !a_actor->IsInCombat() && !state->IsBleedingOut() && !state->IsSwimming() && !a_actor->IsOnMount() &&
			       state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal;
		}

		// Walk With Me's Relax camp keeps its companions where they are, and a follower told to
		// wait is waiting. Neither runs off to a fight out of their sight, often cells away; one
		// that comes to them, they meet.
		bool StaysPut(const Party::Member& a_member, RE::Actor* a_actor, Formation a_formation)
		{
			return (a_member.managedByWalkWithMe && a_formation == Formation::kRelax) ||
			       a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) > 0.0f;
		}

		// Why a companion out of a fight cannot join it right now, or empty if they can. A
		// seat, a saddle or water is no reason: the combat AI gets them out of those itself.
		std::string Unable(RE::Actor* a_actor)
		{
			const auto* state = a_actor->AsActorState();
			if (!Game::Usable(a_actor)) {
				return "not loaded";
			}
			if (state->IsBleedingOut() || state->IsUnconscious()) {
				return "they are down";
			}
			return {};
		}

		// The nearest enemy a_looker can see within a_range, or null. Lines of sight are
		// rays, so only the nearest few are tried.
		RE::Actor* FirstSeen(RE::Actor* a_looker, std::span<const RE::NiPointer<RE::Actor>> a_enemies, float a_range)
		{
			std::vector<std::pair<float, RE::Actor*>> inRange;
			for (const auto& enemy : a_enemies) {
				if (const float d = Game::Distance(a_looker, enemy.get()); d <= a_range) {
					inRange.emplace_back(d, enemy.get());
				}
			}
			std::sort(inRange.begin(), inRange.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			bool unused = false;
			for (std::size_t i = 0; i < std::min(inRange.size(), kNearest); ++i) {
				if (a_looker->HasLineOfSight(inRange[i].second, unused)) {
					return inRange[i].second;
				}
			}
			return nullptr;
		}

		// Sets a companion on an enemy unless they were set on them a moment ago, or too often
		// already. True only the first time the combat AI takes it, so it is announced once.
		// a_held learns why when they have been asked too often. The count only holds back a
		// fight that keeps falling through: one they stayed in for a while clears it.
		bool Engage(const Party::Member& a_member, RE::Actor* a_actor, RE::Actor* a_enemy, std::string_view a_why, std::string* a_held = nullptr)
		{
			auto& watch = WatchOf(a_member.id);
			auto& ask = AskedOf(a_member.id, a_enemy->GetHandle());
			ask.idle = 0.0f;
			if (ask.tries >= kEngageTries) {
				if (a_held) {
					*a_held = std::format("asked {} times to take on {}; the combat AI never kept them in it", ask.tries, Game::Name(a_enemy));
				}
				return false;
			}
			if (watch.engageIn > 0.0f) {
				return false;
			}
			watch.engageIn = kEngageEvery;
			++ask.tries;
			const bool taken = a_actor->StartCombat(a_enemy);
			// A fight the other side knows nothing about does not hold: it falls apart within a
			// second. So an enemy not yet at any of us is set on them too.
			bool turned = false;
			if (taken) {
				auto*      player = RE::PlayerCharacter::GetSingleton();
				const auto on = Game::CombatTarget(a_enemy);
				if (!a_enemy->IsInCombat() || !on || (on.get() != player && !Party::IsPartyActor(on.get()))) {
					turned = a_enemy->StartCombat(a_actor);
				}
			}
			Log::Info("[Sight] {} {} {} ({:.0f} away){}{}{}."sv, a_member.name, a_why, Game::Name(a_enemy), Game::Distance(a_actor, a_enemy),
				ask.tries > 1 ? std::format("; asked {} times now", ask.tries) : std::string(), taken ? "" : "; the combat AI refused",
				turned ? "; they turn on them" : "");
			if (!taken || ask.told) {
				return false;
			}
			ask.told = true;
			return true;
		}

		// One line per enemy, however many companions went for them.
		void Announce(const std::vector<std::pair<std::string, std::vector<std::string>>>& a_told)
		{
			for (const auto& [enemy, names] : a_told) {
				std::string who;
				for (std::size_t i = 0; i < names.size(); ++i) {
					who += i == 0 ? "" : i + 1 == names.size() ? Tr(" and ") : Tr(", ");
					who += names[i];
				}
				Notices::Push(names.size() == 1 ? Trf("{} takes on {}.", who, enemy) : Trf("{} take on {}.", who, enemy), Notices::Kind::kOrder);
			}
		}

		// Only the weapons we brought out, and only on companions still out of a fight.
		void PutAway(std::string_view a_why)
		{
			int count = 0;
			for (auto& watch : watches) {
				if (!watch.drew) {
					continue;
				}
				watch.drew = false;
				auto* member = Party::Find(watch.id);
				auto  actor = member ? member->handle.get() : nullptr;
				if (actor && Free(actor.get()) && actor->AsActorState()->IsWeaponDrawn()) {
					actor->DrawWeaponMagicHands(false);
					++count;
				}
			}
			if (count > 0) {
				Log::Info("[Sight] {}: {} companion(s) put their weapons away."sv, a_why, count);
			}
		}

		void EndSighting()
		{
			if (sighted && undone > 0) {
				Log::Info("[Sight] The game put the weapons away again on {} companion(s) while the enemy was in view; left that way."sv, undone);
			}
			sighted = false;
			undone = 0;
			for (auto& watch : watches) {
				watch.tried = false;
			}
		}
	}

	void Update(float a_dt)
	{
		float dt = 0.0f;
		if (!Due(a_dt, dt)) {
			return;
		}

		const auto& settings = Settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		auto*       lists = RE::ProcessLists::GetSingleton();
		if (!player || !lists || !player->Is3DLoaded()) {
			return;
		}
		const bool playerDrawn = player->AsActorState()->IsWeaponDrawn();
		// Without auto battle nothing happens before a fight: no weapons out, no fight started.
		if (!settings.autoBattle) {
			EndSighting();
			if (!playerDrawn) {
				PutAway("Auto battle is off"sv);
			}
			return;
		}
		if (Paused()) {
			return;
		}

		std::vector<RE::NiPointer<RE::Actor>> enemies;
		for (const auto& handle : lists->highActorHandles) {
			auto actor = handle.get();
			if (!actor || actor.get() == player || Game::Distance(player, actor.get()) > settings.sightRange + kLeadSlack) {
				continue;
			}
			if (Enemy(actor.get(), player)) {
				enemies.push_back(std::move(actor));
			}
		}

		struct Looker
		{
			Party::Member*           member;
			RE::NiPointer<RE::Actor> actor;
			RE::Actor*               sees{ nullptr };
		};
		std::vector<Looker> lookers;
		RE::Actor*          first = enemies.empty() ? nullptr : FirstSeen(player, enemies, settings.sightRange);
		std::string         seer = first ? "you"s : ""s;
		for (auto& member : Party::Members()) {
			auto actor = member.summon ? nullptr : member.handle.get();
			if (!actor || !Free(actor.get())) {
				continue;
			}
			Looker looker{ &member, actor };
			if (!enemies.empty()) {
				looker.sees = FirstSeen(actor.get(), enemies, settings.sightRange);
				if (looker.sees && !first) {
					first = looker.sees;
					seer = member.name;
				}
			}
			lookers.push_back(std::move(looker));
		}

		if (!first) {
			clearFor += dt;
			if (clearFor >= kLostAfter) {
				EndSighting();
			}
			if (Rules::SheathesAfterSight(clearFor, kSheatheAfter, playerDrawn)) {
				PutAway("Nothing in view"sv);
			}
			return;
		}
		clearFor = 0.0f;
		if (!sighted) {
			sighted = true;
			Log::Info("[Sight] {} in view, {:.0f} away (seen by {}); {} hostile within reach."sv, Game::Name(first), Game::Distance(player, first), seer,
				enemies.size());
		}

		const bool sneaking = player->IsSneaking();
		if (Rules::DrawsOnSight(settings.autoBattle, settings.drawOnSight, sneaking)) {
			std::string drew;
			// Once a sighting: asking again each time the game puts them away only has them
			// drawing and sheathing for as long as the enemy stays in view.
			for (auto& looker : lookers) {
				auto&      watch = WatchOf(looker.member->id);
				const bool drawn = looker.actor->AsActorState()->IsWeaponDrawn();
				if (watch.tried) {
					if (watch.drew && !drawn) {
						watch.drew = false;
						++undone;
					}
					continue;
				}
				if (drawn) {
					continue;
				}
				looker.actor->DrawWeaponMagicHands(true);
				watch.tried = true;
				watch.drew = true;
				drew += std::format("{}{}", drew.empty() ? "" : ", ", looker.member->name);
			}
			if (!drew.empty()) {
				Log::Info("[Sight] Weapons out: {}."sv, drew);
			}
		}

		std::vector<std::pair<std::string, std::vector<std::string>>> told;
		for (auto& looker : lookers) {
			const auto* orders = looker.sees ? Party::FindOrders(looker.member->id) : nullptr;
			if (!orders || !Rules::StartsFight(orders->position, sneaking)) {
				continue;
			}
			if (Engage(*looker.member, looker.actor.get(), looker.sees, "starts the fight with"sv)) {
				auto enemy = Game::Name(looker.sees);
				auto it = std::find_if(told.begin(), told.end(), [&](const auto& a_told) { return a_told.first == enemy; });
				if (it == told.end()) {
					told.emplace_back(std::move(enemy), std::vector<std::string>{});
					it = std::prev(told.end());
				}
				it->second.push_back(looker.member->name);
			}
		}
		Announce(told);
	}

	void Join(float a_dt)
	{
		float dt = 0.0f;
		if (!Due(a_dt, dt)) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded() || Paused()) {
			return;
		}
		const auto& settings = Settings::Get();
		// Anyone already fighting you or one of them is everyone's fight, whatever the game
		// thinks of them otherwise (a wolf, a summon's fire). With auto battle, so is any enemy
		// in view who has not come at any of you yet, unless you are sneaking.
		const bool open = settings.autoBattle && !player->IsSneaking();

		struct Foe
		{
			RE::NiPointer<RE::Actor> actor;
			RE::NiPointer<RE::Actor> on;  // the player or the companion they are fighting
		};
		std::vector<Foe>                      foes;
		std::vector<RE::NiPointer<RE::Actor>> others;  // auto battle: hostile, but at none of you yet
		const std::size_t                     listed = Enemies::All().size();
		auto known = [&](RE::Actor* a_enemy) {
			return std::any_of(foes.begin(), foes.end(), [&](const Foe& a_foe) { return a_foe.actor.get() == a_enemy; }) ||
			       std::any_of(others.begin(), others.end(), [&](const auto& a_other) { return a_other.get() == a_enemy; });
		};
		auto consider = [&](RE::NiPointer<RE::Actor> a_enemy) {
			if (!a_enemy || known(a_enemy.get()) || !Opponent(a_enemy.get(), player)) {
				return;
			}
			if (auto on = Game::CombatTarget(a_enemy.get()); on && (on.get() == player || Party::IsPartyActor(on.get()))) {
				foes.push_back({ std::move(a_enemy), std::move(on) });
			} else if (open && a_enemy->IsHostileToActor(player) && !KeepsToItself(a_enemy.get())) {
				others.push_back(std::move(a_enemy));
			}
		};
		for (const auto& profile : Enemies::All()) {
			consider(profile.handle.get());
		}
		// Whoever is on you or them counts even before a combat group lists them; with auto
		// battle, so does any enemy near enough to be seen.
		if (auto* lists = RE::ProcessLists::GetSingleton()) {
			for (const auto& handle : lists->highActorHandles) {
				auto actor = handle.get();
				if (actor && actor.get() != player && (open || actor->IsInCombat()) &&
					Game::Distance(player, actor.get()) <= settings.sightRange + kLeadSlack) {
					consider(std::move(actor));
				}
			}
		}

		const auto formation = WalkWithMe::CurrentFormation();
		for (auto& member : Party::Members()) {
			auto actor = member.summon ? nullptr : member.handle.get();
			if (!actor) {
				continue;
			}
			auto& watch = WatchOf(member.id);
			if (actor->IsInCombat()) {
				if (watch.outFor >= kTellOutAfter) {
					Log::Info("[Sight] {} is in the fight now, after {:.1f} s out of it."sv, member.name, watch.outFor);
				}
				watch.outFor = 0.0f;
				watch.held.clear();
				// In combat with no combat AI to run it, nothing here or in the planner moves them.
				watch.bareFor = Game::Controller(actor.get()) ? 0.0f : watch.bareFor + dt;
				if (watch.bareFor >= kTellOutAfter && !watch.bareTold) {
					watch.bareTold = true;
					Log::Info("[Sight] {} is in combat but has had no combat AI for {:.1f} s."sv, member.name, watch.bareFor);
				}
				watch.inFor += dt;
				if (watch.inFor >= kKeptAfter) {
					for (auto& ask : asked) {
						if (ask.member == member.id) {
							ask.tries = 0;
						}
					}
				}
				continue;
			}
			watch.inFor = 0.0f;
			watch.outFor += dt;
			std::string held = Unable(actor.get());
			const auto* orders = Party::FindOrders(member.id);
			if (held.empty() && !orders) {
				held = "no orders yet";
			}
			RE::Actor*  target = nullptr;
			std::string why;
			if (held.empty()) {
				// Hold back starts nothing: a bow or spell joins in on whoever is already at it, a
				// blade only on whoever comes near or is on them.
				const bool holdBack = orders->position == Position::kHoldBack;
				const bool ranged = Rules::IsRanged(orders->role);
				const bool staysPut = StaysPut(member, actor.get(), formation);
				const Foe* pick = nullptr;
				float      best = 0.0f;
				float      nearest = -1.0f;
				for (const auto& foe : foes) {
					const float d = Game::Distance(actor.get(), foe.actor.get());
					const bool  onThem = foe.on.get() == actor.get();
					nearest = nearest < 0.0f ? d : std::min(nearest, d);
					if (holdBack && !ranged && !onThem && d > settings.tuning.holdBackRadius) {
						continue;
					}
					if (!onThem && d > settings.sightRange + kLeadSlack && Game::Distance(player, foe.actor.get()) > settings.sightRange) {
						continue;
					}
					if (staysPut && !onThem && d > settings.sightRange) {
						continue;
					}
					// Whoever is on them first, then the nearest.
					const float score = onThem ? -1.0f : d;
					if (!pick || score < best) {
						pick = &foe;
						best = score;
					}
				}
				if (pick) {
					target = pick->actor.get();
					why = pick->on.get() == actor.get() ? "fights back against"s
					      : pick->on.get() == player    ? "joins your fight against"s
					                                    : std::format("helps {} against", Game::Name(pick->on.get()));
				} else if (open && !holdBack) {
					target = FirstSeen(actor.get(), others, settings.sightRange);
					why = "takes on";
				}
				if (!target && staysPut && !foes.empty()) {
					held = std::format("staying put (Relax camp or told to wait), and the nearest enemy is {:.0f} away", nearest);
				} else if (!target) {
					held = !foes.empty() ? (holdBack ? std::format("on Hold back, and nobody near them is fighting (nearest {:.0f} away)", nearest)
					                                 : std::format("the nearest enemy is {:.0f} away", nearest))
					       : !others.empty() ? (holdBack ? std::format("on Hold back, and none of the {} enemies near has come at any of you", others.size())
					                                     : std::format("none of the {} enemies near is in their view", others.size()))
					                         : std::format("nobody is on you or them ({} in the fight{})", listed, open ? "" : "; auto battle is off");
				}
			}
			if (target) {
				Engage(member, actor.get(), target, why, &held);
			}
			if (!held.empty() && held != watch.held && watch.outFor >= kTellOutAfter) {
				Log::Info("[Sight] {} is still out of the fight after {:.1f} s: {}."sv, member.name, watch.outFor, held);
				watch.held = held;
			}
		}
	}

	void FightStarted()
	{
		for (auto& watch : watches) {
			watch.drew = false;
			watch.engageIn = 0.0f;
			watch.outFor = 0.0f;
			watch.held.clear();
			watch.bareFor = 0.0f;
			watch.bareTold = false;
			watch.inFor = 0.0f;
		}
		EndSighting();
		clearFor = 0.0f;
		lookIn = 0.0f;  // the first look for anyone left out comes at once
	}

	void Reset()
	{
		watches.clear();
		asked.clear();
		leftAlone.clear();
		sighted = false;
		undone = 0;
		clearFor = 0.0f;
		lookIn = 0.0f;
		pending = 0.0f;
	}
}

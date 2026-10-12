#include "FWM/Battle.h"

#include "FWM/Abilities.h"
#include "FWM/Enemies.h"
#include "FWM/Camera.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Hud.h"
#include "FWM/Leash.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Overrides.h"
#include "FWM/Potions.h"
#include "FWM/Specials.h"
#include "FWM/Party.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"
#include "FWM/Styles.h"
#include "FWM/TimeBend.h"
#include "FWM/WalkWithMe.h"

namespace FWM::Battle
{
	namespace
	{
		float rosterIn{ 0.0f };
		float decideIn{ 0.0f };
		float pendingDt{ 0.0f };
		float statsIn{ 30.0f };
		bool  fighting{ false };
		float emptyFor{ 0.0f };  // seconds the fight has had nobody left to fight

		// The last party order, while everyone still has the orders it gave them. For the
		// plan to show; the orders themselves live with each companion.
		Shout shout{ Shout::kNone };

		std::atomic<bool> planOpen{ false };

		std::mutex           commandLock;
		std::vector<Command> commands;

		std::mutex                            viewLock;
		std::shared_ptr<const View::Snapshot> view;
		std::atomic<bool>                     viewQueued{ false };
		std::uint64_t                         viewSerial{ 0 };

		// The slow parts of a companion's view (the character sheet, the heal preview),
		// refreshed four times a second rather than every frame the plan is open.
		struct SlowParts
		{
			View::Member  view;
			std::uint64_t at{ 0 };
		};
		std::unordered_map<RE::FormID, SlowParts> slowParts;
		std::uint64_t                              rosterAt{ 0 };
		constexpr std::uint64_t                    kSlowMs = 250;

		// The last enemy the player hit, for "Your target".
		RE::ActorHandle lastHit;
		std::uint64_t   lastHitAt{ 0 };

		struct HitSink final : RE::BSTEventSink<RE::TESHitEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (a_event && a_event->cause && a_event->cause->IsPlayerRef() && a_event->target) {
					if (auto* actor = a_event->target->As<RE::Actor>()) {
						lastHit = actor->GetHandle();
						lastHitAt = ::GetTickCount64();
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		} hitSink;

		RE::ActorHandle PlayerTarget()
		{
			if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
				if (auto ref = pick->GetActiveTarget().get()) {
					if (auto* actor = ref->As<RE::Actor>(); actor && Enemies::Find(actor->GetFormID())) {
						return actor->GetHandle();
					}
				}
			}
			if (lastHit && ::GetTickCount64() - lastHitAt < 8000) {
				if (auto actor = lastHit.get(); actor && !actor->IsDead() && Enemies::Find(lastHit)) {
					return lastHit;
				}
			}
			return {};
		}

		bool AnyoneFighting(RE::PlayerCharacter* a_player)
		{
			if (a_player->IsInCombat()) {
				return true;
			}
			for (auto& member : Party::Members()) {
				if (auto actor = member.handle.get(); actor && actor->IsInCombat()) {
					return true;
				}
			}
			return false;
		}

		int standDowns{ 0 };  // tries this fight at letting companions out of an empty fight

		bool SetOrders(RE::FormID a_id, const Orders& a_orders);

		// A mark ends when the enemy falls, the fight ends, a party order replaces it or the
		// player calls the companion off. The focus it replaced comes back with it.
		void EndMark(Party::Member& a_member, std::string_view a_why = {})
		{
			const bool had = static_cast<bool>(a_member.marked);
			a_member.marked = {};
			a_member.markFor = 0.0f;
			a_member.markEngageIn = 0.0f;
			a_member.markAsks = 0;
			a_member.markTaken = false;
			a_member.markWarned = false;
			a_member.markStuck = false;
			a_member.markRanged = Role::kCount;
			if (const auto* own = Party::FindOrders(a_member.id); own && own->focus == Focus::kMarked) {
				auto orders = *own;
				orders.focus = a_member.focusBeforeMark.value_or(Info(orders.role).focus);
				if (orders.focus == Focus::kMarked) {
					orders.focus = Info(orders.role).focus;
				}
				if (had && !a_why.empty()) {
					Log::Info("[Target] {}: the mark is over ({}); back to {}."sv, a_member.name, a_why, Info(orders.focus).name);
				}
				SetOrders(a_member.id, orders);
			}
			a_member.focusBeforeMark.reset();
		}

		// Marks with nothing behind them: a focus still on Marked after a load, or a mark left
		// over after the player picked another focus.
		void ForgetStaleMarks()
		{
			for (auto& member : Party::Members()) {
				if (member.summon) {
					continue;
				}
				const auto* own = Party::FindOrders(member.id);
				if (!own) {
					continue;
				}
				if (own->focus == Focus::kMarked && !member.marked) {
					EndMark(member);
				} else if (own->focus != Focus::kMarked && member.marked) {
					member.marked = {};
					member.focusBeforeMark.reset();
				}
			}
		}

		// While the plan is open, an enemy someone is sent after is outlined, through walls, so
		// it can be picked out of the fight. Short outlines renewed while the mark lasts and the
		// plan is up, and taken off the moment it closes: never one without an end.
		std::unordered_map<RE::FormID, float> markGlows;  // enemy -> seconds left on their outline
		constexpr float                       kMarkGlowSeconds = 2.5f;

		void StopMarkGlows()
		{
			markGlows.clear();
			auto* shader = Forms::Get().markGlow;
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!shader || !lists) {
				return;
			}
			lists->ForEachShaderEffect([&](RE::ShaderReferenceEffect* a_effect) {
				if (a_effect && a_effect->effectData == shader) {
					a_effect->finished = true;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void GlowMarks(float a_dt)
		{
			std::vector<RE::NiPointer<RE::Actor>> marked;
			for (auto& member : Party::Members()) {
				if (auto enemy = member.marked.get(); enemy && !enemy->IsDead()) {
					marked.push_back(std::move(enemy));
				}
			}
			for (auto it = markGlows.begin(); it != markGlows.end();) {
				const auto id = it->first;
				if (std::none_of(marked.begin(), marked.end(), [&](const auto& a_enemy) { return a_enemy->GetFormID() == id; })) {
					it = markGlows.erase(it);
				} else {
					it->second -= a_dt;
					++it;
				}
			}
			auto* shader = Forms::Get().markGlow;
			if (!Settings::Get().markGlow || !shader || !planOpen.load()) {
				return;
			}
			for (const auto& enemy : marked) {
				auto& left = markGlows[enemy->GetFormID()];  // a new mark starts at nothing: lit at once
				if (left > 0.5f) {
					continue;
				}
				enemy->ApplyEffectShader(shader, kMarkGlowSeconds);
				left = kMarkGlowSeconds;
			}
		}

		void StandDown()
		{
			Overrides::Publish({});
			Styles::Press({});
			StopMarkGlows();
			Specials::Clear();
			Game::ForgetHeads();
			emptyFor = 0.0f;
			standDowns = 0;
			Abilities::Clear();
			Enemies::Clear();
			for (auto& member : Party::Members()) {
				member.desired = {};
				member.outOfReach.clear();
				member.reachRole = Role::kCount;
				member.guardFor = 0.0f;
				member.burstLeft = 0;
				EndMark(member, "the fight is over"sv);
				member.inCombatFor = 0.0f;
				member.opened = false;
				member.armed = false;
				member.warnedAmmo = false;
				member.rightHand = nullptr;
			}
		}

		// The best bow or crossbow someone carries, and arrows (or bolts) for it.
		struct RangedKit
		{
			RE::TESObjectWEAP* weapon{ nullptr };
			RE::TESAmmo*       ammo{ nullptr };
			std::int32_t       count{ 0 };
		};

		RangedKit KitOf(RE::Actor* a_actor)
		{
			RangedKit kit;
			std::vector<std::pair<RE::TESAmmo*, std::int32_t>> ammo;
			const auto items = a_actor->GetInventory([](RE::TESBoundObject& a_object) { return a_object.IsWeapon() || a_object.IsAmmo(); });
			for (const auto& [object, entry] : items) {
				if (!object || entry.first <= 0) {
					continue;
				}
				if (auto* weapon = object->As<RE::TESObjectWEAP>(); weapon && (weapon->IsBow() || weapon->IsCrossbow())) {
					if (!kit.weapon || weapon->GetAttackDamage() > kit.weapon->GetAttackDamage()) {
						kit.weapon = weapon;
					}
				} else if (auto* arrows = object->As<RE::TESAmmo>()) {
					ammo.emplace_back(arrows, entry.first);
				}
			}
			if (!kit.weapon) {
				return kit;
			}
			const bool bolts = kit.weapon->IsCrossbow();
			auto*      nocked = a_actor->GetCurrentAmmo();
			for (auto& [arrows, count] : ammo) {
				if (arrows->IsBolt() != bolts) {
					continue;
				}
				if (arrows == nocked) {  // what they already use wins
					kit.ammo = arrows;
					kit.count = count;
					break;
				}
				if (count > kit.count) {
					kit.ammo = arrows;
					kit.count = count;
				}
			}
			return kit;
		}

		std::string KitText(const RangedKit& a_kit)
		{
			if (!a_kit.weapon) {
				return "no bow";
			}
			return a_kit.ammo ? std::format("{} and {} {}", a_kit.weapon->GetName(), a_kit.count, a_kit.ammo->GetName()) :
			                    std::format("{} but nothing to shoot", a_kit.weapon->GetName());
		}

		// An archer with a bow and nothing to shoot: said once a fight, in the log and on screen.
		void OutOfArrows(Party::Member& a_member, const RangedKit& a_kit)
		{
			if (a_member.warnedAmmo || !a_kit.weapon) {
				return;
			}
			a_member.warnedAmmo = true;
			Log::Info("[Style] {} has {} but no arrows; they fight as a Fighter until they get some."sv, a_member.name, a_kit.weapon->GetName());
			Notices::Push(std::format("{} has no arrows for {}.", a_member.name, a_kit.weapon->GetName()), Notices::Kind::kWarning);
		}

		// What a ranged companion holds. Steel at the start of a fight is what they wore
		// out of it, and the engine keeps a drawn weapon at least five seconds: an archer
		// gets their bow at once instead. Every switch after that is logged with what the
		// engine had to go on, so a dagger drawn mid-fight can be explained.
		void WatchHands(Party::Member& a_member, RE::Actor* a_actor, const Orders& a_orders, Position a_resolved, RE::CombatController* a_controller)
		{
			auto*       right = a_actor->GetEquippedObject(false);
			const auto* weapon = right ? right->As<RE::TESObjectWEAP>() : nullptr;
			const bool  steel = weapon && weapon->IsMelee();
			if (!a_member.armed) {
				a_member.armed = true;
				if (a_orders.role == Role::kArcher) {
					const auto kit = KitOf(a_actor);
					auto*      equip = RE::ActorEquipManager::GetSingleton();
					if (kit.weapon && !kit.ammo) {
						OutOfArrows(a_member, kit);
					} else if (steel && a_resolved != Position::kFrontLine && kit.weapon && equip) {
						if (a_actor->GetCurrentAmmo() != kit.ammo) {
							equip->EquipObject(a_actor, kit.ammo);
						}
						equip->EquipObject(a_actor, kit.weapon);
						Log::Info("[Style] {} came into the fight holding {}; drawing {}."sv, a_member.name, weapon->GetName(), KitText(kit));
					}
				}
				a_member.rightHand = right;
				return;
			}
			if (right == a_member.rightHand) {
				return;
			}
			a_member.rightHand = right;
			if (!weapon || !Rules::IsRanged(a_orders.role)) {
				return;
			}
			if (!steel) {
				Log::Info("[Style] {} back to {}."sv, a_member.name, weapon->GetName());
				return;
			}
			auto       target = a_controller->targetHandle.get();
			bool       unused = false;
			const bool seen = target && a_actor->HasLineOfSight(target.get(), unused);
			const auto kit = KitOf(a_actor);
			Log::Info("[Style] {} ({}, {}) switches to {}; target {} {:.0f} away{}; carries {}."sv, a_member.name, Info(a_orders.role).name,
				Info(a_resolved).name, weapon->GetName(), target ? Game::Name(target.get()) : std::string("none"),
				target ? Game::Distance(a_actor, target.get()) : 0.0f, seen ? "" : ", out of sight", KitText(kit));
			if (a_orders.role == Role::kArcher && !kit.ammo) {
				OutOfArrows(a_member, kit);
			}
		}

		// Weapons out with nobody left to fight: let companions out of combat so they fall
		// back into formation instead of standing in a search.
		void LeaveIfEmpty(float a_dt)
		{
			if (!Enemies::All().empty()) {
				emptyFor = 0.0f;
				return;
			}
			emptyFor += a_dt;
			const auto& settings = Settings::Get();
			// Pulled straight back in twice already: something we cannot see keeps the fight
			// going, so try less often rather than sheathing and drawing every few seconds.
			const float wait = settings.leaveAfter * (standDowns >= 2 ? 4.0f : 1.0f);
			if (!settings.leaveEmptyFights || emptyFor < wait) {
				return;
			}
			int                      left = 0;
			std::vector<std::string> holding;
			for (auto& member : Party::Members()) {
				if (auto actor = member.handle.get(); actor && actor->IsInCombat() && !actor->IsDead()) {
					if (auto target = Game::CombatTarget(actor.get())) {
						auto name = Game::Name(target.get());
						if (std::find(holding.begin(), holding.end(), name) == holding.end()) {
							holding.push_back(std::move(name));
						}
					}
					actor->StopCombat();
					++left;
				}
			}
			if (left) {
				std::string why;
				for (const auto& name : holding) {
					why += why.empty() ? name : ", " + name;
				}
				auto* player = RE::PlayerCharacter::GetSingleton();
				Log::Info("[Battle] No enemies left for {:.1f} s; {} companion(s) stand down (still targeting: {}{})."sv, emptyFor, left,
					why.empty() ? "nobody"s : why, player && player->IsInCombat() ? "; you are in combat"sv : ""sv);
				++standDowns;
			}
			emptyFor = 0.0f;
		}

		// When the combat AI finds no path to its target it notes where the target stood
		// (CombatState::unreachableLocations), and while the target is near such a spot it
		// circles and backs off instead of closing. Read on the main thread like the rest of
		// the controller; the list is short and only positions are read.
		// The layout is CommonLib's and checked here only by size, so the list is read under a
		// structured exception guard: a wrong guess reads as "reachable", never as a crash.
		bool NearUnreachableSpot(const RE::CombatState* a_state, float a_x, float a_y, float a_z)
		{
			__try {
				const auto& spots = a_state->unreachableLocations;
				const auto  count = spots.size();
				if (count == 0 || count > 64 || spots.capacity() < count) {
					return false;
				}
				const RE::UnreachableLocation* data = spots.data();
				for (std::uint32_t i = 0; i < count; ++i) {
					const float dx = data[i].loc.pos.x - a_x, dy = data[i].loc.pos.y - a_y, dz = data[i].loc.pos.z - a_z;
					if (dx * dx + dy * dy + dz * dz < 320.0f * 320.0f) {
						return true;
					}
				}
				return false;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool Unreachable(RE::CombatController* a_controller, RE::Actor* a_target)
		{
			auto* state = a_controller ? a_controller->state : nullptr;
			if (!state || !a_target) {
				return false;
			}
			const auto at = a_target->GetPosition();
			return NearUnreachableSpot(state, at.x, at.y, at.z);
		}

		// A mark that cannot be reached on foot: shoot or cast at it if they can, otherwise
		// say so and let them go back to their own focus rather than circle it for ever.
		void MarkOutOfReach(Party::Member& a_member, RE::Actor* a_target, std::string_view a_why)
		{
			a_member.markStuck = true;
			const auto target = Game::Name(a_target);
			if (a_member.arsenal.ranged && a_member.arsenal.ammo) {
				a_member.markRanged = Role::kArcher;
			} else if (a_member.arsenal.casts) {
				a_member.markRanged = Role::kMage;
			}
			if (a_member.markRanged != Role::kCount) {
				const bool bow = a_member.markRanged == Role::kArcher;
				Log::Info("[Target] {} cannot reach {} ({}); {} instead."sv, a_member.name, target, a_why, bow ? "shoots"sv : "casts"sv);
				Notices::Push(std::format("{} can't reach {}: {} instead.", a_member.name, target, bow ? "shooting" : "casting"), Notices::Kind::kWarning);
				return;
			}
			Log::Info("[Target] {} cannot reach {} ({}) and has no bow or spells; the order is dropped."sv, a_member.name, target, a_why);
			Notices::Push(std::format("{} can't reach {}.", a_member.name, target), Notices::Kind::kWarning);
			EndMark(a_member, "cannot reach them"sv);
		}

		// A melee companion who never closes on their target: usually the path (a ledge,
		// water, a locked gate), not the orders. One line per target, after eight seconds.
		bool OutOfReach(const Party::Member& a_member, const RE::ActorHandle& a_enemy)
		{
			return std::any_of(a_member.outOfReach.begin(), a_member.outOfReach.end(), [&](const auto& a_entry) { return a_entry.first == a_enemy; });
		}

		// An enemy a melee companion cannot get to on foot: an archer on a ledge, someone across
		// water or a gap. They are left alone for a while (the targeting rules skip them), then
		// tried again; coming within reach of them ends it at once.
		constexpr float kOutOfReachSeconds = 25.0f;

		void NoteOutOfReach(Party::Member& a_member, RE::Actor* a_actor, RE::Actor* a_enemy, std::string_view a_how)
		{
			const auto handle = a_enemy->GetHandle();
			if (OutOfReach(a_member, handle)) {
				return;
			}
			a_member.outOfReach.emplace_back(handle, kOutOfReachSeconds);
			Log::Info("[Target] {} cannot reach {} on foot ({:.0f} away; {}); fights someone else for {:.0f} s."sv, a_member.name, Game::Name(a_enemy),
				Game::Distance(a_actor, a_enemy), a_how, kOutOfReachSeconds);
		}

		void AgeReach(Party::Member& a_member, RE::Actor* a_actor, float a_dt)
		{
			std::erase_if(a_member.outOfReach, [&](auto& a_entry) {
				a_entry.second -= a_dt;
				auto enemy = a_entry.first.get();
				return a_entry.second <= 0.0f || !enemy || enemy->IsDead() || Game::Distance(a_actor, enemy.get()) < 400.0f;
			});
		}

		// A melee companion who never closes on their target: usually the path (a ledge,
		// water, a locked gate), not the orders. After eight seconds without getting closer,
		// the target counts as out of reach for them.
		void Chase(Party::Member& a_member, RE::Actor* a_actor, RE::CombatController* a_controller, const RE::ActorHandle& a_target, float a_dt)
		{
			auto target = a_target.get();
			if (!target) {
				a_member.chasing = {};
				return;
			}
			const float d = Game::Distance(a_actor, target.get());
			const bool same = a_member.chasing == a_target;
			if (!same || d < a_member.chasingFrom - 150.0f || d < 400.0f) {
				// Progress, or a new target: start the clock again.
				a_member.chasingLogged = same && a_member.chasingLogged && d >= 400.0f;
				a_member.chasing = a_target;
				a_member.chasingFrom = d;
				a_member.chasingFor = 0.0f;
				a_member.chasingAt = a_actor->GetPosition();
				return;
			}
			a_member.chasingFor += a_dt;
			if (a_member.chasingFor > 8.0f && !a_member.chasingLogged) {
				// No closer, but running all the while: an archer backing off as fast as they come,
				// not a ledge. Keep after them. Out of reach is someone circling in one spot below.
				const bool engineSays = Unreachable(a_controller, target.get());
				if (!engineSays && a_actor->GetPosition().GetDistance(a_member.chasingAt) > 350.0f) {
					a_member.chasingFrom = d;
					a_member.chasingFor = 0.0f;
					a_member.chasingAt = a_actor->GetPosition();
					return;
				}
				a_member.chasingLogged = true;
				NoteOutOfReach(a_member, a_actor, target.get(), engineSays ? "the combat AI found no path either"sv : "no closer in 8 s, circling in place"sv);
			}
		}

		// An enemy a taunt holds that keeps its distance from the tank, for the log: whether
		// pressing works, and on what kind of fighter it does not.
		struct HoldWatch
		{
			float closest{ std::numeric_limits<float>::max() };
			float stuckFor{ 0.0f };
			bool  logged{ false };
		};
		std::unordered_map<RE::FormID, HoldWatch> holdWatch;

		void WatchHold(RE::Actor* a_enemy, RE::Actor* a_tank, float a_dt)
		{
			auto&       watch = holdWatch[a_enemy->GetFormID()];
			const float d = Game::Distance(a_enemy, a_tank);
			if (d < watch.closest - 100.0f || d < 350.0f) {
				watch.closest = std::min(watch.closest, d);
				watch.stuckFor = 0.0f;
				return;
			}
			watch.stuckFor += a_dt;
			if (watch.stuckFor > 6.0f && !watch.logged) {
				watch.logged = true;
				const auto* profile = Enemies::Find(a_enemy->GetFormID());
				const auto  kind = profile && profile->archer ? "archer"sv : (profile && profile->caster ? "caster"sv : "melee"sv);
				auto*       controller = Game::Controller(a_enemy);
				Log::Info("[Taunt] {} ({}) is held by {} but stays {:.0f} away after {:.0f} s{}."sv, Game::Name(a_enemy), kind, Game::Name(a_tank), d,
					watch.stuckFor, Unreachable(controller, a_tank) ? "; the combat AI found no path to them" : "");
			}
		}

		// The combat AI only takes a target its combat group already knows, so an enemy that
		// has not fought this companion yet (or a companion not fighting at all) would be
		// refused over and over. StartCombat adds the enemy to their fight, as a script's
		// StartCombat does; asked again every two seconds until it holds.
		void Engage(Party::Member& a_member, RE::Actor* a_actor, RE::CombatController* a_controller, float a_dt)
		{
			auto marked = a_member.marked.get();
			if (!marked) {
				return;
			}
			a_member.markFor += a_dt;
			a_member.markEngageIn -= a_dt;
			const bool inFight = a_actor->IsInCombat() && a_controller;
			if (inFight && Overrides::Takes(a_controller, a_member.marked)) {
				if (a_controller->targetHandle == a_member.marked) {
					if (!a_member.markTaken) {
						a_member.markTaken = true;
						Log::Info("[Target] {} is on {} ({:.0f} away, {:.1f} s after the order)."sv, a_member.name, Game::Name(marked.get()),
							Game::Distance(a_actor, marked.get()), a_member.markFor);
					}
				} else if (!a_member.markWarned && a_member.markFor > 4.0f) {
					a_member.markWarned = true;
					auto other = a_controller->targetHandle.get();
					Log::Info("[Target] {} was sent after {} {:.0f} s ago but fights {}{}."sv, a_member.name, Game::Name(marked.get()), a_member.markFor,
						other ? Game::Name(other.get()) : "nobody"s, a_member.reviving ? " (on the way to heal someone)" : "");
				}
				return;
			}
			if (a_member.markEngageIn > 0.0f) {
				return;
			}
			a_member.markEngageIn = 2.0f;
			const bool taken = a_actor->StartCombat(marked.get());
			if (++a_member.markAsks == 1 || a_member.markAsks == 5) {
				const auto refused = taken ? ""s : std::format("; refused (try {})", a_member.markAsks);
				if (inFight) {
					Log::Info("[Target] {}'s fight did not include {}; asked the combat AI to add them{}."sv, a_member.name, Game::Name(marked.get()), refused);
				} else {
					Log::Info("[Target] {} was out of combat; asked the combat AI to start a fight with {}{}."sv, a_member.name, Game::Name(marked.get()), refused);
				}
			}
		}

		void Decide(RE::PlayerCharacter* a_player, float a_dt)
		{
			const auto& settings = Settings::Get();
			const auto  formation = WalkWithMe::CurrentFormation();
			const auto  playerHandle = a_player->GetHandle();
			const auto  playerTarget = PlayerTarget();
			const float heading = a_player->GetAngleZ();
			const RE::NiPoint3 forward{ std::sin(heading), std::cos(heading), 0.0f };

			std::vector<Overrides::Entry> entries;
			auto&                         profiles = Enemies::All();

			// Who each companion is on right now, for spreading out.
			std::vector<std::pair<RE::FormID, RE::ActorHandle>> engaged;
			for (auto& member : Party::Members()) {
				if (auto actor = member.handle.get(); actor) {
					if (auto* controller = Game::Controller(actor.get())) {
						engaged.emplace_back(member.id, controller->targetHandle);
					}
				}
			}

			for (auto& member : Party::Members()) {
				if (member.summon) {
					continue;
				}
				auto actor = member.handle.get();
				auto* controller = actor ? Game::Controller(actor.get()) : nullptr;
				if (member.marked) {
					auto marked = member.marked.get();
					if (!marked || marked->IsDead() || marked->IsDisabled()) {
						EndMark(member, marked && marked->IsDead() ? "they are dead"sv : "they are gone"sv);
					} else if (actor && Game::Usable(actor.get()) && !actor->AsActorState()->IsBleedingOut()) {
						Engage(member, actor.get(), controller, a_dt);
						controller = Game::Controller(actor.get());  // StartCombat may have made one
					}
				}
				if (!actor || !Game::Usable(actor.get()) || !actor->IsInCombat() || !controller) {
					member.desired = {};
					member.armed = false;
					member.warnedAmmo = false;
					member.rightHand = nullptr;
					continue;
				}
				const auto* own = Party::FindOrders(member.id);
				if (!own) {
					continue;
				}
				const auto orders = *own;
				const auto resolved = Resolve(orders.position, formation);
				const bool down = actor->AsActorState()->IsBleedingOut();
				if (member.down && !down) {
					Log::Info("[Battle] {} is back up: {}, {}."sv, member.name, Info(orders.role).name, Info(resolved).name);
				}
				member.down = down;
				// An archer with nothing to shoot fights as a Fighter: a dagger kept for point
				// blank would lose to a bow that cannot fire anyway, and on Hold back they take
				// on only what comes near them instead of running across the field.
				auto fightAs = orders;
				if (orders.role == Role::kArcher && member.arsenalKnown && !member.arsenal.ammo) {
					fightAs.role = Role::kFighter;
				}
				// Who they cannot get to on foot: forget those they reached after all, and note the
				// one they are on if the combat AI has already found there is no way.
				AgeReach(member, actor.get(), a_dt);
				if (!Rules::IsRanged(fightAs.role)) {
					if (auto on = controller->targetHandle.get(); on && Unreachable(controller, on.get())) {
						NoteOutOfReach(member, actor.get(), on.get(), "the combat AI found no path"sv);
					}
				}
				// Sent after a marked enemy: no leash holds them, and they go straight at it and
				// stay on it. Guard, Hold back and With me make melee slow to commit, a tank's
				// dueling and shield keep them circling at arm's length, and the combat AI circles
				// while it waits a turn: all of it reads as ignoring the order.
				bool markLive = member.marked && orders.focus == Focus::kMarked;
				if (markLive && !member.markStuck && !Rules::IsRanged(fightAs.role) && OutOfReach(member, member.marked)) {
					if (auto mark = member.marked.get()) {
						MarkOutOfReach(member, mark.get(), "no way there on foot"sv);
					}
					markLive = member.marked && Party::FindOrders(member.id) && Party::FindOrders(member.id)->focus == Focus::kMarked;
				}
				if (markLive && member.markRanged != Role::kCount) {
					fightAs.role = member.markRanged;
				} else if (!Rules::IsRanged(fightAs.role)) {
					// Nobody left that they can get to: a bow or spells, if they have them, beat
					// standing under a ledge.
					const bool nobody = !member.outOfReach.empty() && std::all_of(profiles.begin(), profiles.end(), [&](const auto& a_profile) {
						auto enemy = a_profile.handle.get();
						return !enemy || enemy->IsDead() || OutOfReach(member, a_profile.handle);
					});
					Role instead = Role::kCount;
					if (nobody && member.arsenal.ranged && member.arsenal.ammo) {
						instead = Role::kArcher;
					} else if (nobody && member.arsenal.casts) {
						instead = Role::kMage;
					}
					if (instead != member.reachRole) {
						if (instead != Role::kCount) {
							Log::Info("[Target] {} can reach nobody on foot; {} instead."sv, member.name, instead == Role::kArcher ? "shoots"sv : "casts"sv);
						} else if (member.reachRole != Role::kCount) {
							Log::Info("[Target] {} has someone in reach again."sv, member.name);
						}
						member.reachRole = instead;
					}
					if (instead != Role::kCount) {
						fightAs.role = instead;
					}
				}
				const bool charge = markLive && !Rules::IsRanged(fightAs.role);
				Styles::Apply(actor.get(), fightAs, charge ? Position::kFrontLine : resolved, charge);
				if (auto patient = member.reviving.get()) {
					// A little inside arm's reach, so they arrive close enough to heal.
					Leash::Apply(actor.get(), std::max(settings.tuning.reviveDistance - 60.0f, 100.0f), patient.get());
					member.leashOnPatient = true;
				} else if (member.guardFor > 0.0f) {
					// Bodyguard: at your side, whatever their position.
					Leash::Apply(actor.get(), 220.0f, a_player);
					member.leashOnPatient = true;
				} else if (settings.guardLeash || member.leashOnPatient) {
					member.leashOnPatient = false;
					float leash = 0.0f;
					if (markLive) {
						leash = 0.0f;
					} else if (resolved == Position::kGuard) {
						leash = settings.tuning.guardRadius;
					} else if (resolved == Position::kHoldBack) {
						leash = settings.holdBackLeash;
					} else if (resolved == Position::kWithMe) {
						leash = settings.tuning.withMeRadius + 150.0f;  // room to fight what reaches you
					}
					Leash::Apply(actor.get(), settings.guardLeash ? leash : 0.0f);
				}

				const auto engine = Overrides::EngineChoice(reinterpret_cast<std::uintptr_t>(controller));
				std::vector<Rules::EnemyView> views;
				views.reserve(profiles.size());
				for (const auto& profile : profiles) {
					auto enemy = profile.handle.get();
					Rules::EnemyView v;
					if (enemy) {
						v.distance = Game::Distance(actor.get(), enemy.get());
						v.distanceToPlayer = Game::Distance(a_player, enemy.get());
						const auto offset = enemy->GetPosition() - a_player->GetPosition();
						v.forward = offset.x * forward.x + offset.y * forward.y;
					} else {
						v.distance = v.distanceToPlayer = std::numeric_limits<float>::max();
					}
					v.threat = profile.threat;
					v.health = profile.health;
					v.caster = profile.caster;
					v.healer = profile.healer;
					v.archer = profile.archer;
					auto victim = profile.target.get();
					v.attackingPlayer = profile.target == playerHandle;
					v.attackingParty = victim && Party::IsPartyActor(victim.get());
					v.attackingSelf = profile.target == member.handle;
					v.playerTarget = playerTarget && profile.handle == playerTarget;
					v.marked = member.marked && profile.handle == member.marked;
					v.current = controller->targetHandle == profile.handle;
					v.engineChoice = engine != 0 && engine == Game::Native(profile.handle);
					v.outOfReach = OutOfReach(member, profile.handle);
					for (const auto& [id, on] : engaged) {
						if (id != member.id && on == profile.handle) {
							++v.alliesOn;
						}
					}
					views.push_back(v);
				}

				WatchHands(member, actor.get(), orders, resolved, controller);

				auto focus = orders.focus;
				if (focus == Focus::kMarked && !member.marked) {
					focus = Focus::kEngine;
				}
				const auto pick = Rules::PickTarget(views, focus, resolved, Rules::IsRanged(fightAs.role), settings.tuning);
				auto desired = pick.index >= 0 ? profiles[static_cast<std::size_t>(pick.index)].handle : RE::ActorHandle{};
				// On the way to a downed companion: the engine's own pick, which keeps to the
				// small area around them, not someone across the field.
				if (member.reviving) {
					desired = {};
				}
				// Shield up against archers: turned to the nearest of them, closing under it.
				if (member.shielding && member.shieldFacing && !markLive && !OutOfReach(member, member.shieldFacing)) {
					desired = member.shieldFacing;
				}
				// Closing on someone in melee, shield up or not: if it never happens, they are out of reach.
				if (!Rules::IsRanged(fightAs.role) && (resolved == Position::kFree || resolved == Position::kFrontLine || markLive || member.shielding)) {
					Chase(member, actor.get(), controller, desired ? desired : controller->targetHandle, a_dt);
				} else {
					member.chasing = {};
				}
				if (pick.defending && desired != member.desired) {
					const auto& chosen = profiles[static_cast<std::size_t>(pick.index)];
					auto        enemy = chosen.handle.get();
					Log::Info("[Target] {} turns on {}, who is on them ({:.0f} away), over {}."sv, member.name, chosen.name,
						enemy ? Game::Distance(actor.get(), enemy.get()) : 0.0f, Info(focus).name);
				} else if (settings.logDecisions && desired != member.desired) {
					const auto* chosen = pick.index >= 0 ? &profiles[static_cast<std::size_t>(pick.index)] : nullptr;
					Log::Info("[Target] {}: {} ({} / {})."sv, member.name, chosen ? chosen->name : "their own choice"s,
						Info(focus).name, Info(resolved).name);
				}
				// Bodyguard: whoever is on you, the nearest first.
				if (member.guardFor > 0.0f) {
					float closest = std::numeric_limits<float>::max();
					for (const auto& profile : profiles) {
						auto enemy = profile.handle.get();
						if (enemy && Game::Usable(enemy.get()) && profile.target == playerHandle) {
							if (const float d = Game::Distance(a_player, enemy.get()); d < closest) {
								closest = d;
								desired = profile.handle;
							}
						}
					}
				}
				// A volley: every archer and mage on the one enemy.
				if (const auto volley = Specials::VolleyTarget();
					volley && (fightAs.role == Role::kArcher || fightAs.role == Role::kMage) && Overrides::Takes(controller, volley)) {
					desired = volley;
				}
				member.desired = desired;
				entries.emplace_back(reinterpret_cast<std::uintptr_t>(controller), Game::Native(member.handle), Game::Native(desired),
					Rules::IsRanged(orders.role));
			}

			// Enemies held by a taunt want the tank, whatever their own AI thinks, and press them
			// instead of circling at a distance waiting a turn.
			std::vector<RE::NiPointer<RE::Actor>> held;
			for (const auto& hold : Abilities::Holds()) {
				auto enemy = hold.enemy.get();
				if (auto* controller = enemy ? Game::Controller(enemy.get()) : nullptr) {
					entries.emplace_back(reinterpret_cast<std::uintptr_t>(controller), Game::Native(hold.enemy), Game::Native(hold.tank));
					if (auto tank = hold.tank.get()) {
						WatchHold(enemy.get(), tank.get(), a_dt);
					}
					held.push_back(std::move(enemy));
				}
			}
			std::erase_if(holdWatch, [&](const auto& a_watch) {
				return std::none_of(held.begin(), held.end(), [&](const auto& a_enemy) { return a_enemy->GetFormID() == a_watch.first; });
			});
			std::vector<RE::Actor*> raw;
			raw.reserve(held.size());
			for (const auto& enemy : held) {
				raw.push_back(enemy.get());
			}
			Styles::Press(raw);
			Overrides::Publish(std::move(entries));
		}

		// --- Orders from the menu -------------------------------------------

		Orders ApplyPreset(const Orders& a_orders, Battle::Preset a_preset)
		{
			Orders o = a_orders;
			switch (a_preset) {
			case Preset::kRoles:
				return ForRole(o.role, o);
			case Preset::kProtectMe:
				o.position = Position::kGuard;
				o.focus = o.role == Role::kHealer ? Focus::kAttackingParty : Focus::kAttackingPlayer;
				return o;
			case Preset::kAllOut:
				o.position = Rules::IsRanged(o.role) ? Position::kFree : Position::kFrontLine;
				o.focus = Info(o.role).focus;
				return o;
			case Preset::kHoldBack:
				o.position = Position::kHoldBack;
				o.focus = Focus::kAttackingParty;
				return o;
			}
			return o;
		}

		// One line per change, so the log can say what a companion was told.
		bool SetOrders(RE::FormID a_id, const Orders& a_orders)
		{
			const auto* before = Party::FindOrders(a_id);
			if (before && *before == a_orders) {
				return false;
			}
			Party::SetOrders(a_id, a_orders);
			const auto* member = Party::Find(a_id);
			std::string position{ Info(a_orders.position).name };
			if (a_orders.position == Position::kFormation) {
				position += std::format(" (for now {})", Info(Resolve(a_orders.position, WalkWithMe::CurrentFormation())).name);
			}
			std::string ability;
			if (a_orders.role == Role::kTank || a_orders.role == Role::kHealer) {
				ability = a_orders.autoAbility ? ", on their own" : ", only when told";
				if (a_orders.role == Role::kHealer) {
					ability += std::format(", below {}%", a_orders.healThreshold);
				}
			}
			Log::Info("[Orders] {}: {}, {}, {}{}."sv, member ? member->name : std::format("{:08X}", a_id), Info(a_orders.role).name,
				Info(a_orders.focus).name, position, ability);
			return true;
		}

		// A party order writes everyone's orders. Nobody keeps their own; that is the point.
		void GiveOrder(Shout a_shout)
		{
			bool changed = false;
			for (auto& member : Party::Members()) {
				member.outOfReach.clear();  // a fresh order: try everyone again
				member.reachRole = Role::kCount;
				if (const auto* own = member.summon ? nullptr : Party::FindOrders(member.id)) {
					changed = SetOrders(member.id, Shouted(*own, a_shout)) || changed;
				}
			}
			const bool repeat = shout == a_shout && !changed;
			shout = a_shout;
			decideIn = 0.0f;
			const auto& info = Info(a_shout);
			if (!repeat) {
				Log::Info("[Orders] Party: {} {}"sv, info.name, info.summary);
			}
			Notices::Push(std::format("{}  {}", info.name, info.summary), Notices::Kind::kOrder);
		}

		void ApplyCommands()
		{
			std::vector<Command> pending;
			{
				const std::scoped_lock guard(commandLock);
				pending.swap(commands);
			}
			for (const auto& command : pending) {
				auto* member = Party::Find(command.member);
				switch (command.type) {
				case Command::Type::kSetOrders:
					if (member) {
						member->outOfReach.clear();
						member->reachRole = Role::kCount;
					}
					if (const auto lack = Party::LackFor(command.member, command.orders.role); lack != Lack::kNothing) {
						Log::Info("[Party] {} refused for {:08X}: {}"sv, Info(command.orders.role).name, command.member, Why(lack));
						break;
					}
					SetOrders(command.member, command.orders);
					shout = Shout::kNone;  // someone was told something else
					break;
				case Command::Type::kMark:
					if (member) {
						auto* profile = command.enemy ? Enemies::Find(command.enemy) : nullptr;
						if (!profile) {
							EndMark(*member, "called off"sv);
							break;
						}
						auto orders = Party::FindOrders(command.member) ? *Party::FindOrders(command.member) : Orders{};
						if (orders.focus != Focus::kMarked) {
							member->focusBeforeMark = orders.focus;
						}
						member->marked = profile->handle;
						member->markFor = 0.0f;
						member->markEngageIn = 0.0f;
						member->markAsks = 0;
						member->markTaken = false;
						member->markWarned = false;
						member->markStuck = false;
						member->markRanged = Role::kCount;
						// Sent after them on purpose: an old "no way there" note gets one more try.
						std::erase_if(member->outOfReach, [&](const auto& a_note) { return a_note.first == profile->handle; });
						orders.focus = Focus::kMarked;
						SetOrders(command.member, orders);
						shout = Shout::kNone;
						auto actor = member->handle.get();
						auto enemy = profile->handle.get();
						Log::Info("[Orders] {} is sent after {} ({:.0f} away)."sv, member->name, profile->name,
							actor && enemy ? Game::Distance(actor.get(), enemy.get()) : 0.0f);
						GlowMarks(0.0f);
					}
					break;
				case Command::Type::kTauntNow:
					if (member) {
						member->tauntRequested = true;
						member->askedAt = ::GetTickCount64();
					}
					break;
				case Command::Type::kSpecialNow:
					if (member) {
						member->specialRequested = true;
						member->askedAt = ::GetTickCount64();
					}
					break;
				case Command::Type::kHealNow:
					if (member) {
						member->healRequested = true;
						member->askedAt = ::GetTickCount64();
					}
					break;
				case Command::Type::kShout:
					GiveOrder(command.shout);
					break;
				case Command::Type::kPreset:
					for (auto& each : Party::Members()) {
						each.outOfReach.clear();
						each.reachRole = Role::kCount;
						if (const auto* orders = each.summon ? nullptr : Party::FindOrders(each.id)) {
							SetOrders(each.id, ApplyPreset(*orders, command.preset));
						}
					}
					break;
				}
			}
			if (!pending.empty()) {
				decideIn = 0.0f;  // act on it at once
			}
		}

		// --- The view ---------------------------------------------------------

		View::Point Screen(const RE::NiPoint3& a_point)
		{
			View::Point p;
			if (const auto projected = Game::Project(a_point)) {
				p.x = projected->first;
				p.y = projected->second;
				p.on = p.x >= 0.0f && p.x <= 1.0f && p.y >= 0.0f && p.y <= 1.0f;
			}
			return p;
		}

		// A circle on the ground, a little above it so it is not lost in the floor.
		std::vector<View::Point> GroundRing(const RE::NiPoint3& a_centre, float a_radius, int a_segments)
		{
			std::vector<View::Point> ring;
			ring.reserve(static_cast<std::size_t>(a_segments) + 1);
			for (int i = 0; i <= a_segments; ++i) {
				const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(a_segments);
				ring.push_back(Screen({ a_centre.x + std::cos(a) * a_radius, a_centre.y + std::sin(a) * a_radius, a_centre.z + 8.0f }));
			}
			return ring;
		}

		// Seen from above: where someone stands and a ring a little wider than they are.
		void Footprint(RE::Actor* a_actor, View::Point& a_foot, std::vector<View::Point>& a_ring)
		{
			const auto  at = a_actor->GetPosition();
			const auto  min = a_actor->GetBoundMin(), max = a_actor->GetBoundMax();
			const float width = std::max(max.x - min.x, max.y - min.y) * 0.5f * a_actor->GetScale();
			a_foot = Screen({ at.x, at.y, at.z + 8.0f });
			a_ring = GroundRing(at, std::clamp(width + 22.0f, 40.0f, 220.0f), 24);
		}

		// What the party view shows of a companion: who they are, what they can take and deal,
		// what they hold, and what they are best at.
		void Sheet(View::Member& a_member, RE::Actor* a_actor)
		{
			if (auto* race = a_actor->GetRace()) {
				a_member.race = race->GetFullName();
			}
			if (auto* base = a_actor->GetActorBase(); base && base->npcClass) {
				a_member.className = base->npcClass->GetFullName();
			}
			a_member.healthNow = Game::Current(a_actor, RE::ActorValue::kHealth);
			a_member.healthMax = Game::Maximum(a_actor, RE::ActorValue::kHealth);
			a_member.magickaNow = Game::Current(a_actor, RE::ActorValue::kMagicka);
			a_member.magickaMax = Game::Maximum(a_actor, RE::ActorValue::kMagicka);
			a_member.staminaNow = Game::Current(a_actor, RE::ActorValue::kStamina);
			a_member.staminaMax = Game::Maximum(a_actor, RE::ActorValue::kStamina);
			a_member.armor = a_actor->CalcArmorRating();
			if (const auto kit = KitOf(a_actor); kit.weapon) {
				a_member.bow = true;
				a_member.ammoName = kit.ammo ? kit.ammo->GetName() : "";
				a_member.ammoCount = kit.count;
			}
			a_member.resist = { Game::Current(a_actor, RE::ActorValue::kResistFire), Game::Current(a_actor, RE::ActorValue::kResistFrost),
				Game::Current(a_actor, RE::ActorValue::kResistShock), Game::Current(a_actor, RE::ActorValue::kPoisonResist),
				Game::Current(a_actor, RE::ActorValue::kResistMagic) };
			for (bool left : { false, true }) {
				auto* form = a_actor->GetEquippedObject(left);
				(left ? a_member.leftHand : a_member.rightHand) = form ? form->GetName() : "";
				if (auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr) {
					a_member.damage = std::max(a_member.damage, static_cast<float>(weapon->GetAttackDamage()));
				}
			}
			static constexpr std::array<std::pair<RE::ActorValue, std::string_view>, 12> combat{ {
				{ RE::ActorValue::kOneHanded, "One-Handed" },
				{ RE::ActorValue::kTwoHanded, "Two-Handed" },
				{ RE::ActorValue::kArchery, "Archery" },
				{ RE::ActorValue::kBlock, "Block" },
				{ RE::ActorValue::kHeavyArmor, "Heavy Armor" },
				{ RE::ActorValue::kLightArmor, "Light Armor" },
				{ RE::ActorValue::kSneak, "Sneak" },
				{ RE::ActorValue::kDestruction, "Destruction" },
				{ RE::ActorValue::kRestoration, "Restoration" },
				{ RE::ActorValue::kConjuration, "Conjuration" },
				{ RE::ActorValue::kAlteration, "Alteration" },
				{ RE::ActorValue::kIllusion, "Illusion" },
			} };
			for (const auto& [value, name] : combat) {
				a_member.skills.emplace_back(std::string(name), static_cast<int>(Game::Current(a_actor, value)));
			}
			std::stable_sort(a_member.skills.begin(), a_member.skills.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			a_member.skills.resize(std::min<std::size_t>(a_member.skills.size(), 6));
		}

		void FillSlowParts(View::Member& a_member, RE::Actor* a_actor, std::uint64_t a_now)
		{
			auto& cached = slowParts[a_member.id];
			if (cached.at == 0 || a_now - cached.at >= kSlowMs) {
				View::Member fresh;
				Sheet(fresh, a_actor);
				const auto stock = Potions::StockOf(a_actor);
				fresh.potionHealing = stock.healing;
				fresh.potionMagicka = stock.magicka;
				fresh.potionStamina = stock.stamina;
				fresh.potionOther = stock.other;
				if (!a_member.summon) {
					if (const auto heal = Abilities::PreviewHeal(a_actor)) {
						fresh.canHeal = true;
						fresh.healSpell = heal->spell;
						fresh.healBorrowed = heal->borrowed;
						fresh.healAmount = heal->amount;
						fresh.healCost = heal->cost;
					}
				}
				cached.view = std::move(fresh);
				cached.at = a_now;
			}
			const auto& s = cached.view;
			a_member.race = s.race;
			a_member.className = s.className;
			a_member.healthNow = s.healthNow;
			a_member.healthMax = s.healthMax;
			a_member.magickaNow = s.magickaNow;
			a_member.magickaMax = s.magickaMax;
			a_member.staminaNow = s.staminaNow;
			a_member.staminaMax = s.staminaMax;
			a_member.armor = s.armor;
			a_member.damage = s.damage;
			a_member.rightHand = s.rightHand;
			a_member.leftHand = s.leftHand;
			a_member.bow = s.bow;
			a_member.ammoName = s.ammoName;
			a_member.ammoCount = s.ammoCount;
			a_member.skills = s.skills;
			a_member.resist = s.resist;
			a_member.canHeal = s.canHeal;
			a_member.healSpell = s.healSpell;
			a_member.healBorrowed = s.healBorrowed;
			a_member.healAmount = s.healAmount;
			a_member.healCost = s.healCost;
			a_member.potionHealing = s.potionHealing;
			a_member.potionMagicka = s.potionMagicka;
			a_member.potionStamina = s.potionStamina;
			a_member.potionOther = s.potionOther;
		}

		void BuildView()
		{
			ApplyCommands();

			auto  snapshot = std::make_shared<View::Snapshot>();
			auto* player = RE::PlayerCharacter::GetSingleton();
			snapshot->serial = ++viewSerial;
			snapshot->tuning = Settings::Get().tuning;
			snapshot->walkWithMe = WalkWithMe::Connected();
			snapshot->lendHeal = Settings::Get().healFallback;
			snapshot->formation = WalkWithMe::CurrentFormation();
			snapshot->shout = shout;
			snapshot->inGame = player && player->Is3DLoaded();
			if (!snapshot->inGame) {
				const std::scoped_lock guard(viewLock);
				view = std::move(snapshot);
				return;
			}
			const auto now = ::GetTickCount64();
			if (!fighting && now - rosterAt >= kSlowMs) {
				// Out of combat the roster is only refreshed twice a second; the plan wants it sooner.
				Party::Refresh();
				rosterAt = now;
			}
			snapshot->playerLevel = player->GetLevel();
			snapshot->inCombat = AnyoneFighting(player);
			snapshot->driving = Camera::Driving();
			snapshot->overhead = Camera::Overhead();
			if (snapshot->overhead) {
				snapshot->playerHead = Screen(Game::HeadPosition(player) + RE::NiPoint3{ 0.0f, 0.0f, 22.0f });
				Footprint(player, snapshot->playerFoot, snapshot->playerRing);
			}

			for (auto& member : Party::Members()) {
				auto actor = member.handle.get();
				if (!actor) {
					continue;
				}
				View::Member m;
				m.id = member.id;
				m.name = member.name;
				m.level = actor->GetLevel();
				m.summon = member.summon;
				m.walkWithMe = member.managedByWalkWithMe;
				m.arsenal = member.arsenal;
				m.arsenalKnown = member.arsenalKnown;
				if (const auto* orders = Party::FindOrders(member.id)) {
					m.orders = *orders;
				}
				m.resolved = Resolve(m.orders.position, snapshot->formation);
				m.health = Game::Fraction(actor.get(), RE::ActorValue::kHealth);
				m.magicka = Game::Fraction(actor.get(), RE::ActorValue::kMagicka);
				m.stamina = Game::Fraction(actor.get(), RE::ActorValue::kStamina);
				m.inCombat = actor->IsInCombat();
				m.down = actor->AsActorState()->IsBleedingOut();
				if (auto target = Game::CombatTarget(actor.get())) {
					m.target = target->GetFormID();
					m.targetName = Game::Name(target.get());
				}
				if (auto marked = member.marked.get()) {
					m.marked = marked->GetFormID();
				}
				m.tauntReady = member.tauntReady;
				m.tauntCooldown = member.tauntCooldown;
				m.asked = member.tauntRequested || member.healRequested || member.specialRequested;
				m.brewing = member.brewing >= 0.0f;
				if (member.answerAt && now - member.answerAt < 6000) {
					m.answer = member.answer;
					m.answerDone = member.answerDone;
					m.answerAge = static_cast<float>(now - member.answerAt) / 1000.0f;
				}
				m.held = member.held;
				if (!member.summon) {
					const auto taunt = Abilities::PreviewTaunt(actor.get());
					m.tauntRadius = taunt.radius;
					m.tauntCertain = taunt.certainUpTo;
					m.tauntNever = taunt.noChanceFrom;
					m.tauntHold = taunt.hold;
					m.tauntStamina = taunt.stamina;
					m.healReady = member.healReady;
					m.special = SpecialOf(m.orders.role);
					m.specialReady = member.specialReady;
					m.specialCooldown = member.specialCooldown;
					m.guarding = member.guardFor > 0.0f;
					if (m.orders.role == Role::kTank) {
						m.ring = GroundRing(actor->GetPosition(), taunt.radius, 40);
					}
				}
				m.head = Screen(Game::HeadPosition(actor.get()) + RE::NiPoint3{ 0.0f, 0.0f, 22.0f });
				if (snapshot->overhead) {
					Footprint(actor.get(), m.foot, m.footRing);
				}
				FillSlowParts(m, actor.get(), now);
				snapshot->party.push_back(std::move(m));
			}

			const auto playerHandle = player->GetHandle();
			for (const auto& profile : Enemies::All()) {
				auto actor = profile.handle.get();
				if (!actor) {
					continue;
				}
				View::Enemy e;
				e.id = profile.id;
				e.name = profile.name;
				e.level = profile.level;
				e.health = profile.health;
				e.caster = profile.caster;
				e.healer = profile.healer;
				e.archer = profile.archer;
				e.boss = profile.boss;
				if (auto target = profile.target.get()) {
					e.target = target->GetFormID();
				}
				e.targetsPlayer = profile.target == playerHandle;
				if (auto tank = Abilities::HolderOf(profile.handle).get()) {
					e.heldBy = tank->GetFormID();
				}
				e.head = Screen(Game::HeadPosition(actor.get()) + RE::NiPoint3{ 0.0f, 0.0f, 26.0f });
				if (snapshot->overhead) {
					Footprint(actor.get(), e.foot, e.footRing);
				}
				snapshot->enemies.push_back(std::move(e));
			}

			const std::scoped_lock guard(viewLock);
			view = std::move(snapshot);
		}
	}

	void Update(float a_dt)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		TimeBend::Update();
		ApplyCommands();
		if (!Settings::Get().enabled) {
			// Switched off mid-fight: let go of every target and style at once.
			if (fighting) {
				StandDown();
				Styles::ReleaseAll();
				fighting = false;
				Log::Info("[Battle] Disabled; companions are back to their own judgement."sv);
			}
			return;
		}

		rosterIn -= a_dt;
		if (rosterIn <= 0.0f) {
			Party::Refresh(0.5f - rosterIn);
			ForgetStaleMarks();
			rosterIn = 0.5f;
		}

		Potions::Rest(a_dt);
		if (!AnyoneFighting(player)) {
			if (fighting) {
				StandDown();
				fighting = false;
				Log::Info("[Battle] Combat over."sv);
			}
			return;
		}
		if (!fighting) {
			fighting = true;
			decideIn = 0.0f;
			Party::Refresh();
			Log::Info("[Battle] Combat with {} companion(s)."sv, Party::Members().size());
		}

		// Arrows cross a room in under a second: the shield is checked every frame, not on the planner's tick.
		Abilities::BlockIncoming(a_dt);
		Specials::Tick(a_dt);

		pendingDt += a_dt;
		decideIn -= a_dt;
		if (decideIn <= 0.0f) {
			Enemies::Update(pendingDt);
			Abilities::Update(pendingDt);
			Specials::Update(pendingDt);
			Potions::Fight(pendingDt);
			Decide(player, pendingDt);
			GlowMarks(pendingDt);
			LeaveIfEmpty(pendingDt);
			pendingDt = 0.0f;
			decideIn = Settings::Get().planInterval;
		}

		statsIn -= a_dt;
		if (statsIn <= 0.0f) {
			statsIn = 30.0f;
			const auto stats = Overrides::TakeStats();
			Log::Info("[Hook] {} selector calls, {} ours, {} redirected, {} refused by the engine's test{}."sv,
				stats.calls, stats.matched, stats.overridden, stats.rejected,
				stats.bound ? std::format("; bound weapons refused {} times", stats.bound) : std::string());
		}
	}

	void Reset()
	{
		StandDown();
		fighting = false;
		shout = Shout::kNone;
		Party::ResetRuntime();
		slowParts.clear();
		lastHit = {};
		if (planOpen.exchange(false)) {
			TimeBend::End();
			Hud::Restore();
		}
		Camera::Reset();
	}

	void InstallEvents()
	{
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESHitEvent>(&hitSink);
		}
	}

	void RequestOpen()
	{
		if (planOpen.exchange(true)) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([] {
				TimeBend::Begin();
				Hud::Hide();
				Camera::Begin();
				GlowMarks(0.0f);
				for (auto& member : Party::Members()) {
					member.arsenalIn = 0.0f;  // gear may have changed since the last look
				}
				slowParts.clear();
				rosterAt = 0;
				BuildView();
			});
		}
	}

	void RequestClose()
	{
		if (!planOpen.exchange(false)) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([] {
				ApplyCommands();
				TimeBend::End();
				Hud::Restore();
				Camera::End();
				StopMarkGlows();
				if (!fighting) {
					Game::ForgetHeads();
				}
			});
		}
	}

	bool PlanOpen() noexcept
	{
		return planOpen.load();
	}

	void Queue(const Command& a_command)
	{
		const std::scoped_lock guard(commandLock);
		commands.push_back(a_command);
	}

	void RefreshView()
	{
		if (viewQueued.exchange(true)) {
			return;
		}
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([] {
				viewQueued = false;
				BuildView();
			});
		} else {
			viewQueued = false;
		}
	}

	std::shared_ptr<const View::Snapshot> ReadView()
	{
		const std::scoped_lock guard(viewLock);
		return view;
	}
}

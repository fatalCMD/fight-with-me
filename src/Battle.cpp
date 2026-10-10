#include "FWM/Battle.h"

#include "FWM/Abilities.h"
#include "FWM/Enemies.h"
#include "FWM/Camera.h"
#include "FWM/Game.h"
#include "FWM/Hud.h"
#include "FWM/Leash.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Overrides.h"
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

		void StandDown()
		{
			Overrides::Publish({});
			emptyFor = 0.0f;
			standDowns = 0;
			Abilities::Clear();
			Enemies::Clear();
			for (auto& member : Party::Members()) {
				member.desired = {};
				member.marked = {};
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

		// A melee companion who never closes on their target: usually the path (a ledge,
		// water, a locked gate), not the orders. One line per target, after eight seconds.
		void Chase(Party::Member& a_member, RE::Actor* a_actor, const RE::ActorHandle& a_target, float a_dt)
		{
			auto target = a_target.get();
			if (!target) {
				a_member.chasing = {};
				return;
			}
			const float d = Game::Distance(a_actor, target.get());
			const bool same = a_member.chasing == a_target;
			if (!same || d < a_member.chasingFrom - 150.0f || d < 400.0f) {
				// Progress, or a new target: start the clock again. Once said for a target, it
				// is not said again until they reach it.
				a_member.chasingLogged = same && a_member.chasingLogged && d >= 400.0f;
				a_member.chasing = a_target;
				a_member.chasingFrom = d;
				a_member.chasingFor = 0.0f;
				return;
			}
			a_member.chasingFor += a_dt;
			if (a_member.chasingFor > 8.0f && !a_member.chasingLogged) {
				a_member.chasingLogged = true;
				Log::Info("[Target] {} has not closed on {} in {:.0f} s ({:.0f} away); the way there may be blocked."sv, a_member.name,
					Game::Name(target.get()), a_member.chasingFor, d);
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
				Styles::Apply(actor.get(), fightAs, resolved);
				if (auto patient = member.reviving.get()) {
					// A little inside arm's reach, so they arrive close enough to heal.
					Leash::Apply(actor.get(), std::max(settings.tuning.reviveDistance - 60.0f, 100.0f), patient.get());
					member.leashOnPatient = true;
				} else if (settings.guardLeash || member.leashOnPatient) {
					member.leashOnPatient = false;
					float leash = 0.0f;
					if (resolved == Position::kGuard) {
						leash = settings.tuning.guardRadius;
					} else if (resolved == Position::kHoldBack) {
						leash = settings.holdBackLeash;
					} else if (resolved == Position::kWithMe) {
						leash = settings.tuning.withMeRadius + 150.0f;  // room to fight what reaches you
					}
					Leash::Apply(actor.get(), settings.guardLeash ? leash : 0.0f);
				}

				if (member.marked) {
					if (auto marked = member.marked.get(); !marked || marked->IsDead()) {
						member.marked = {};
					}
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
				if (member.shielding && member.shieldFacing) {
					desired = member.shieldFacing;
				}
				// Closing on someone in melee: say so if it never happens.
				if (!Rules::IsRanged(fightAs.role) && (resolved == Position::kFree || resolved == Position::kFrontLine) && !member.shielding) {
					Chase(member, actor.get(), desired ? desired : controller->targetHandle, a_dt);
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
				member.desired = desired;
				entries.emplace_back(reinterpret_cast<std::uintptr_t>(controller), Game::Native(member.handle), Game::Native(desired),
					Rules::IsRanged(orders.role));
			}

			// Enemies held by a taunt want the tank, whatever their own AI thinks.
			for (const auto& hold : Abilities::Holds()) {
				auto enemy = hold.enemy.get();
				if (auto* controller = enemy ? Game::Controller(enemy.get()) : nullptr) {
					entries.emplace_back(reinterpret_cast<std::uintptr_t>(controller), Game::Native(hold.enemy), Game::Native(hold.tank));
				}
			}
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
					if (const auto lack = Party::LackFor(command.member, command.orders.role); lack != Lack::kNothing) {
						Log::Info("[Party] {} refused for {:08X}: {}"sv, Info(command.orders.role).name, command.member, Why(lack));
						break;
					}
					SetOrders(command.member, command.orders);
					shout = Shout::kNone;  // someone was told something else
					break;
				case Command::Type::kMark:
					if (member) {
						auto* profile = Enemies::Find(command.enemy);
						member->marked = profile ? profile->handle : RE::ActorHandle{};
						if (profile) {
							auto orders = Party::FindOrders(command.member) ? *Party::FindOrders(command.member) : Orders{};
							orders.focus = Focus::kMarked;
							SetOrders(command.member, orders);
							shout = Shout::kNone;
						}
					}
					break;
				case Command::Type::kTauntNow:
					if (member) {
						member->tauntRequested = true;
					}
					break;
				case Command::Type::kHealNow:
					if (member) {
						member->healRequested = true;
					}
					break;
				case Command::Type::kShout:
					GiveOrder(command.shout);
					break;
				case Command::Type::kPreset:
					for (auto& each : Party::Members()) {
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
				m.held = member.held;
				if (!member.summon) {
					const auto taunt = Abilities::PreviewTaunt(actor.get());
					m.tauntRadius = taunt.radius;
					m.tauntCertain = taunt.certainUpTo;
					m.tauntNever = taunt.noChanceFrom;
					m.tauntHold = taunt.hold;
					m.tauntStamina = taunt.stamina;
					m.healReady = member.healReady;
					if (m.orders.role == Role::kTank) {
						const auto centre = actor->GetPosition();
						constexpr int segments = 40;
						m.ring.reserve(segments + 1);
						for (int i = 0; i <= segments; ++i) {
							const float a = 6.2831853f * static_cast<float>(i) / segments;
							m.ring.push_back(Screen({ centre.x + std::cos(a) * taunt.radius, centre.y + std::sin(a) * taunt.radius, centre.z + 8.0f }));
						}
					}
				}
				m.head = Screen(Game::HeadPosition(actor.get()) + RE::NiPoint3{ 0.0f, 0.0f, 22.0f });
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
			rosterIn = 0.5f;
		}

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

		pendingDt += a_dt;
		decideIn -= a_dt;
		if (decideIn <= 0.0f) {
			Enemies::Update(pendingDt);
			Abilities::Update(pendingDt);
			Decide(player, pendingDt);
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

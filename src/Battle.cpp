#include "FWM/AbilityAuto.h"
#include "FWM/Battle.h"
#include "FWM/PartyBalance.h"
#include "FWM/EffectAudio.h"

#include "FWM/Abilities.h"
#include "FWM/AbilityLink.h"
#include "FWM/Arts.h"
#include "FWM/Berserks.h"
#include "FWM/Surges.h"
#include "FWM/ArcherAmmo.h"
#include "FWM/Enemies.h"
#include "FWM/Camera.h"
#include "FWM/Blasts.h"
#include "FWM/DragonBreaths.h"
#include "FWM/Circles.h"
#include "FWM/Effects.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Hud.h"
#include "FWM/Leash.h"
#include "FWM/Leveling.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Orbs.h"
#include "FWM/OrdersLink.h"
#include "FWM/Quakes.h"
#include "FWM/Rainfall.h"
#include "FWM/Cloaks.h"
#include "FWM/TauntBears.h"
#include "FWM/ChaosPulses.h"
#include "FWM/Rejuvenations.h"
#include "FWM/RejuvenateField.h"
#include "FWM/RejuvenatePolicy.h"
#include "FWM/Freeze.h"
#include "FWM/Haunts.h"
#include "FWM/Host.h"
#include "FWM/Overrides.h"
#include "FWM/Specials.h"
#include "FWM/Sunders.h"
#include "FWM/Disarms.h"
#include "FWM/RiposteVfx.h"
#include "FWM/ArrowArts.h"
#include "FWM/Windsteps.h"
#include "FWM/SecondWinds.h"
#include "FWM/AzurasMoon.h"
#include "FWM/TankActs.h"
#include "FWM/WeakWills.h"
#include "FWM/Bastions.h"
#include "FWM/Party.h"
#include "FWM/PathBonus.h"
#include "FWM/PathsLink.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"
#include "FWM/Sight.h"
#include "FWM/Spells.h"
#include "FWM/Loadouts.h"
#include "FWM/Styles.h"
#include "FWM/Text.h"
#include "FWM/TimeBend.h"
#include "FWM/Traits.h"
#include "FWM/WalkWithMe.h"

namespace FWM::Battle
{
	namespace
	{
		float rosterIn{ 0.0f };
		float decideIn{ 0.0f };
		float pendingDt{ 0.0f };
		float statsIn{ 30.0f };
		float glowIn{ 0.0f };  // until the ability glows are brought up to date
		bool  fighting{ false };
		std::atomic<bool> partyFighting{ false };  // companions in a fight: Walk With Me leaves the party alone
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
				// The perk hooks weaken actual damage; this notification also registers blocked
				// and harmless impacts, including magic that a tank completely resisted.
				if (a_event && a_event->cause && a_event->target) {
					TankActs::Attack(a_event->cause->As<RE::Actor>(), a_event->target->As<RE::Actor>(), false);
				}
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

		bool InAFight()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return fighting || (player && AnyoneFighting(player));
		}

		int standDowns{ 0 };  // tries this fight at letting companions out of an empty fight
		std::vector<RE::FormID> wereFighting;  // companions in combat last frame

		bool SetOrders(RE::FormID a_id, const Orders& a_orders);

		// A mark ends when the enemy falls, the fight ends, a party order replaces it or the
		// player calls the companion off. The focus it replaced comes back with it.
		void EndMark(Party::Member& a_member, std::string_view a_why = {})
		{
			const bool had = static_cast<bool>(a_member.marked);
			a_member.marked = {};
			a_member.markFor = 0.0f;
			a_member.markEngageIn = 0.0f;
			a_member.markAway = 0.0f;
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
				if (!member.CanTakeRole()) {
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

		// Every look still playing, at once: a load, or the mod switched off.
		void ClearLooks()
		{
			EffectAudio::Clear();
			Blasts::Clear();
			DragonBreaths::Clear();
			Rainfall::Clear();
			Cloaks::Clear();
			TauntBears::Clear();
			ChaosPulses::Clear();
			Rejuvenations::Clear();
			Haunts::Clear();
			Sunders::Clear();
            Disarms::Clear();
            RiposteVfx::Clear();
            ArrowArts::Clear();
            Windsteps::Clear();
            SecondWinds::Clear();
            AzurasMoon::Clear();
			Berserks::Clear();
            Surges::Clear();
			TankActs::Clear();
			WeakWills::Clear();
			Bastions::Clear();
			Freeze::Clear();
		}

		void StandDown()
		{
            Loadouts::EndBattle();
			DragonBreaths::Clear(); // End the channel before clearing its movement hold.
			Overrides::Publish({});
			Styles::Press({});
			StopMarkGlows();
			Specials::Clear();
			TankActs::Clear();
			Arts::Clear();
			Circles::Clear();
			Orbs::Clear();
			Quakes::Clear();
			Freeze::Clear();
			// The bursts, the rain and the smoke play out though the fight is over (often their own blow
			// ended it); the cloak falls away as Renewal does. ClearLooks takes them at a load.
			Cloaks::EndAll();
				Game::ForgetHeads();
			emptyFor = 0.0f;
			standDowns = 0;
			wereFighting.clear();
			Abilities::Clear();
			AbilityAuto::Clear();
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
				member.drained = false;
				member.drainedTold = false;
				member.handsChecked = false;
				member.spellIdle = 0.0f;
				member.spellReady = {};
				member.spellTold = {};
				if (member.spellsDriven > 0) {
					Log::Info("[Spells] {}: {} chosen spell(s) cast for them this fight; the combat AI {} one itself."sv, member.name, member.spellsDriven,
						member.spellsByAi ? "also cast"sv : "never cast"sv);
				}
				member.spellsByAi = false;
				member.spellsDriven = 0;
			}
		}

		// Mages' and healers' chosen spells, by actor, for the combat AI hooks: kept up out of a
		// fight too, so the AI's first pick when one starts already holds to them.
		void PublishHands()
		{
			std::vector<Overrides::HandsEntry> chosen;
			for (const auto& member : Party::Members()) {
				const auto* orders = !member.CanTakeRole() ? nullptr : Party::FindOrders(member.id);
                if(!orders) continue;
                auto actor=member.handle.get(); if(!actor)continue;
                if(actor->IsInCombat()){Loadouts::BeginBattle(actor.get(),*orders);orders=Party::FindOrders(member.id);}
                Loadouts::Enforce(actor.get(),orders->role);
                auto pools=Loadouts::Effective(member.id);
                if(!ChoosesSpells(orders->role)){pools[0].clear();pools[1].clear();}
                if(!pools[0].empty() || !pools[1].empty() || !pools[2].empty() || orders->role==Role::kBruiser || ChoosesSpells(orders->role)) {
                    Overrides::HandsEntry entry;entry.actor=Game::Native(member.handle);entry.body=actor.get();
                    entry.hands=ChoosesSpells(orders->role)?Loadouts::Hands(member.id):Hands{};
                    entry.pools=std::move(pools);entry.twoHandedOnly=orders->role==Role::kBruiser;
                    entry.roleRestricted=AbilityLink::LoadoutMask(orders->role).has_value();
                    entry.caster=ChoosesSpells(orders->role);entry.recovering=entry.caster && member.drained && actor->IsInCombat();
                    const auto manual=Loadouts::Of(member.id);
                    entry.explicitSpells={!manual[0].empty(),!manual[1].empty()};
                    entry.battleLocked=Loadouts::Locked(member.id);
                    const auto* selected=entry.pools[2].empty()?nullptr:RE::TESForm::LookupByID<RE::TESObjectWEAP>(entry.pools[2].front());
                    entry.staffLocked=entry.battleLocked && selected && selected->IsStaff();
                    chosen.push_back(std::move(entry));
                }
			}
			Overrides::PublishHands(std::move(chosen));
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
				if (auto* weapon = object->As<RE::TESObjectWEAP>(); weapon && (weapon->IsBow() || weapon->IsCrossbow()) && Loadouts::WeaponAllowed(a_actor,weapon,Role::kArcher)) {
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

		// What was chosen for them, in their hands: when the player gives them a role, and when a
		// fight opens with something else there. Their weapon pools, or a mage's or healer's chosen
		// spells with any weapon put away. A role alone changes nothing they hold. Returns what went
		// into their hands, for the log.
		std::string ArmFor(RE::Actor* a_actor, Role a_role)
		{
			if (!a_actor) {
				return {};
			}
			if (Loadouts::Locked(a_actor->GetFormID())) {
				return Loadouts::EquipBattle(a_actor);
			}
			auto* equip = RE::ActorEquipManager::GetSingleton();
			if (!equip || !ChoosesSpells(a_role) || Loadouts::Hands(a_actor->GetFormID()) == Hands{}) {
				return {};
			}
			std::string away;
			for (const bool left : { false, true }) {
				auto* form = a_actor->GetEquippedObject(left);
				if (auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr; weapon && !weapon->IsBound()) {
					equip->UnequipObject(a_actor, weapon);
					away = weapon->GetName();
				}
			}
			auto spells = Spells::EquipChosen(a_actor);
			if (away.empty() && spells.empty()) {
				return {};
			}
			return spells.empty() ? std::format("hands free ({} put away)", away) :
			       away.empty()   ? spells :
			                        std::format("{} ({} put away)", spells, away);
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
			Notices::Push(Trf("{} has no arrows for {}.", a_member.name, a_kit.weapon->GetName()), Notices::Kind::kWarning);
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
			if (!a_member.armed || a_member.armedRole != a_orders.role) {
				a_member.armed = true;
				a_member.armedRole = a_orders.role;
				// Into the fight holding something other than what was chosen for them: the chosen
				// goes into their hands now, once.
				if (const auto armed = ArmFor(a_actor, a_orders.role); !armed.empty()) {
					Log::Info("[Style] {} was holding {}; fighting as {}, equips {}."sv, a_member.name, right ? right->GetName() : "nothing",
						Info(a_orders.role).name, armed);
					right = a_actor->GetEquippedObject(false);
				}
				if (a_orders.role == Role::kArcher) {
					if (const auto kit = KitOf(a_actor); kit.weapon && !kit.ammo) {
						OutOfArrows(a_member, kit);
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

		// Once a fight, a few seconds in: what the combat AI made of a mage's or healer's chosen
		// spells. One it never took up, or took up for something other than an attack, is still
		// theirs to cast; Spells::Drive casts it for them when the AI will not.
		void ReportHands(Party::Member& a_member, RE::Actor* a_actor, RE::CombatController* a_controller, const Orders& a_orders)
		{
			if (a_member.handsChecked || !ChoosesSpells(a_orders.role) || a_member.inCombatFor < 4.0f) {
				return;
			}
			a_member.handsChecked = true;
			// What the combat AI has to choose from and what it holds: whether chosen spells are even in it.
			auto held = [&](bool a_left) {
				auto* form = a_actor->GetEquippedObject(a_left);
				return form && form->GetName() && *form->GetName() ? std::string(form->GetName()) : "nothing"s;
			};
			Log::Info("[Spells] {} holds {} (left) and {} (right); the combat AI's inventory: {}."sv, a_member.name, held(true), held(false),
				Overrides::DescribeInventory(a_controller));
			const auto hands = Loadouts::Hands(a_member.id);
			const auto seen = Overrides::SeenHands(Game::Native(a_member.handle));
			if (hands == Hands{} || !seen) {
				return;
			}
			std::string detail;
			for (const auto hand : { kLeftHand, kRightHand }) {
				if (!hands[hand] || (hand == kRightHand && hands[kRightHand] == hands[kLeftHand])) {
					continue;
				}
				const auto  bits = (*seen)[hand];
				auto*       spell = RE::TESForm::LookupByID<RE::SpellItem>(hands[hand]);
				std::string as;
				for (std::size_t c = 0; c < static_cast<std::size_t>(Overrides::Caster::kCount); ++c) {
					if (bits & (1u << c)) {
						as += std::format("{}{}", as.empty() ? "" : ", ", Overrides::CasterName(static_cast<Overrides::Caster>(c)));
					}
				}
				detail += std::format("{}{}: {}", detail.empty() ? "" : "; ", spell && spell->GetName() ? spell->GetName() : "?",
					as.empty() ? "never taken up"s : "taken up as " + as);
			}
			Log::Info("[Spells] {}'s chosen spells as the combat AI sees them: {}."sv, a_member.name, detail);
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
			} else if (a_member.arsenal.casts && !a_member.drained) {
				a_member.markRanged = Role::kMage;
			}
			if (a_member.markRanged != Role::kCount) {
				const bool bow = a_member.markRanged == Role::kArcher;
				Log::Info("[Target] {} cannot reach {} ({}); {} instead."sv, a_member.name, target, a_why, bow ? "shoots"sv : "casts"sv);
				Notices::Push(bow ? Trf("{} can't reach {}: shooting instead.", a_member.name, target) : Trf("{} can't reach {}: casting instead.", a_member.name, target),
					Notices::Kind::kWarning);
				return;
			}
			Log::Info("[Target] {} cannot reach {} ({}) and has no bow or spells; the order is dropped."sv, a_member.name, target, a_why);
			Notices::Push(Trf("{} can't reach {}.", a_member.name, target), Notices::Kind::kWarning);
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

		// A mark ends this long after its enemy left the fight (fled, yielded, or was left
		// behind), and after this many refusals to add it to the companion's fight. Otherwise
		// the companion is pushed into a new fight with them every two seconds, for ever.
		constexpr float kMarkAwaySeconds = 4.0f;
		constexpr int   kMarkAsks = 5;

		void DropMark(Party::Member& a_member, std::string_view a_why)
		{
			if (auto marked = a_member.marked.get()) {
				Notices::Push(Trf("{} lets {} go: {}.", a_member.name, Game::Name(marked.get()), a_why), Notices::Kind::kWarning);
			}
			EndMark(a_member, a_why);
		}

		// The combat AI only takes a target its combat group already knows, so an enemy that
		// has not fought this companion yet (or a companion not fighting at all) would be
		// refused over and over. StartCombat adds the enemy to their fight, as a script's
		// StartCombat does; asked again every two seconds until it holds, a few times at most.
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
				a_member.markAsks = 0;
				if (a_controller->targetHandle == a_member.marked) {
					if (!a_member.markTaken) {
						a_member.markTaken = true;
						Log::Info("[Target] {} is on {} ({:.0f} away, {:.1f} s after the order)."sv, a_member.name, Game::Name(marked.get()),
							Game::Distance(a_actor, marked.get()), a_member.markFor);
					}
				} else if (!a_member.markWarned && a_member.markFor > 4.0f) {
					a_member.markWarned = true;
					auto other = a_controller->targetHandle.get();
					Log::Info("[Target] {} was sent after {} {:.0f} s ago but fights {}."sv, a_member.name, Game::Name(marked.get()), a_member.markFor,
						other ? Game::Name(other.get()) : "nobody"s);
				}
				return;
			}
			if (a_member.markEngageIn > 0.0f) {
				return;
			}
			if (a_member.markAsks >= kMarkAsks) {
				Log::Info("[Target] {}: the combat AI would not take on {} after {} tries."sv, a_member.name, Game::Name(marked.get()), a_member.markAsks);
				DropMark(a_member, Tr("the combat AI would not take them on"));
				return;
			}
			a_member.markEngageIn = 2.0f;
			const bool taken = a_actor->StartCombat(marked.get());
			if (++a_member.markAsks == 1 && !a_member.markTaken) {
				const auto refused = taken ? ""s : "; refused"s;
				if (inFight) {
					Log::Info("[Target] {}'s fight did not include {}; asked the combat AI to add them{}."sv, a_member.name, Game::Name(marked.get()), refused);
				} else {
					Log::Info("[Target] {} was out of combat; asked the combat AI to start a fight with {}{}."sv, a_member.name, Game::Name(marked.get()), refused);
				}
			}
		}

		// The glows that last as long as an ability does: red on every enemy a taunt holds, gold
		// on a tank while the taunt's armor is on, orange on a volley's target. Each goes out the
		// moment its hold, brace or volley ends.
		void ShowAbilities()
		{
			std::vector<RE::NiPointer<RE::Actor>>     alive;  // kept for the length of the call
			std::vector<std::pair<RE::Actor*, float>> on;
			auto show = [&](Effects::Look a_look) {
				Effects::Keep(a_look, on);
				on.clear();
			};
			auto add = [&](RE::NiPointer<RE::Actor> a_actor, float a_seconds) {
				if (a_actor) {
					on.emplace_back(a_actor.get(), a_seconds);
					alive.push_back(std::move(a_actor));
				}
			};
			// The tank glows while braced or holding anyone, whichever lasts longer.
			std::vector<std::pair<RE::FormID, float>> tanks;
			auto tankFor = [&](RE::FormID a_id, float a_seconds) {
				const auto it = std::find_if(tanks.begin(), tanks.end(), [&](const auto& a_tank) { return a_tank.first == a_id; });
				if (it == tanks.end()) {
					tanks.emplace_back(a_id, a_seconds);
				} else {
					it->second = std::max(it->second, a_seconds);
				}
			};
			for (const auto& hold : Abilities::Holds()) {
				add(hold.enemy.get(), hold.remaining);
				if (auto tank = hold.tank.get()) {
					tankFor(tank->GetFormID(), hold.remaining);
				}
			}
			show(Effects::Look::kHeld);
			for (const auto& [id, left] : Abilities::BraceTimes()) {
				tankFor(id, left);
			}
			for (const auto& [id, seconds] : tanks) {
				add(RE::NiPointer<RE::Actor>(RE::TESForm::LookupByID<RE::Actor>(id)), seconds);
			}
			show(Effects::Look::kBraced);
			add(Specials::VolleyTarget().get(), Specials::VolleyLeft());
			show(Effects::Look::kQuarry);
			Arts::Show();
		}

        // Cost only spells this role and its pools can actually choose. Automatic
        // hands stay native choices, not selected spells with a skill bypass.
        float CastCost(const Party::Member& member, RE::Actor* actor)
        {
            const auto* orders=Party::FindOrders(member.id);
            if(!orders || !ChoosesSpells(orders->role))return member.arsenal.castCost;
            auto pools=Loadouts::Effective(member.id);
            const auto automatic=Spells::Automatic(actor,orders->role);
            float cheapest=0.0f;
            for(std::size_t hand=0;hand<2;++hand) {
                if(pools[hand].empty())pools[hand]=automatic[hand];
                for(auto id:pools[hand])if(auto* spell=RE::TESForm::LookupByID<RE::SpellItem>(id)) {
                    const float cost=spell->CalculateMagickaCost(actor);
                    if(cost>0.0f && (cheapest<=0.0f || cost<cheapest))cheapest=cost;
                }
            }
            return cheapest;
        }

        bool ChargedStaff(RE::Actor* actor, Role role)
        {
            for(const auto& [object,entry]:actor->GetInventory([](RE::TESBoundObject& o){return o.IsWeapon();})) {
                auto* weapon=object?object->As<RE::TESObjectWEAP>():nullptr;
                if(weapon && weapon->IsStaff() && entry.first>0 && entry.second &&
                   Loadouts::WeaponAllowed(actor,weapon,role) && entry.second->GetEnchantmentCharge().value_or(0.0)>0.0)return true;
            }
            return false;
        }

        // An eligible, charged staff can keep casting without magicka. A forbidden
        // dagger or exhausted staff must never cancel recovery.
		void Drain(Party::Member& a_member, RE::Actor* a_actor, const Orders& a_orders)
		{
			const bool  caster = ChoosesSpells(a_orders.role) || a_member.arsenal.casts;
			const float cost = caster && !ChargedStaff(a_actor,a_orders.role) ? CastCost(a_member, a_actor) : 0.0f;
			const float magicka = Game::Current(a_actor, RE::ActorValue::kMagicka);
			const float most = Game::Maximum(a_actor, RE::ActorValue::kMagicka);
			const bool  drained = Rules::OutOfMagicka(magicka, most, cost, a_member.drained);
			if (drained == a_member.drained) {
				return;
			}
			a_member.drained = drained;
			if (!ChoosesSpells(a_orders.role)) {
				return;  // a fighter who also casts: only casting at what they cannot reach stops
			}
			if (!drained) {
				Log::Info("[Style] {} has magicka again ({:.0f} of {:.0f}); back to their place."sv, a_member.name, magicka, most);
				return;
			}
            a_actor->InterruptCast(false);
            a_actor->NotifyAnimationGraph("attackStop");
            a_actor->NotifyAnimationGraph("bashStop");
			Log::Info("[Style] {} is out of magicka ({:.0f} of {:.0f}; the cheapest spell takes {:.0f}); falls back until it comes back."sv, a_member.name,
				magicka, most, cost);
			if (!a_member.drainedTold) {
				a_member.drainedTold = true;
				Notices::Push(Trf("{} is out of magicka and falls back to recover.", a_member.name), Notices::Kind::kWarning);
			}
		}

		void Decide(RE::PlayerCharacter* a_player, float a_dt)
		{
			const auto& settings = Settings::Get();
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
				if (!member.CanTakeRole()) {
					continue;
				}
				auto actor = member.handle.get();
				auto* controller = actor ? Game::Controller(actor.get()) : nullptr;
				if (member.marked) {
					// Still in the fight: among the enemies the player or someone in the party is
					// fighting. Only then is the companion pushed into a fight with them.
					auto marked = member.marked.get();
					member.markAway = marked && Enemies::Find(member.marked) ? 0.0f : member.markAway + a_dt;
					if (!marked || marked->IsDead() || marked->IsDisabled()) {
						EndMark(member, marked && marked->IsDead() ? "they are dead"sv : "they are gone"sv);
					} else if (!marked->Is3DLoaded()) {
						DropMark(member, Tr("they are too far away"));
					} else if (member.markAway > kMarkAwaySeconds) {
						DropMark(member, Tr("they left the fight"));
					} else if (member.markAway == 0.0f && actor && Game::Usable(actor.get()) && !actor->AsActorState()->IsBleedingOut()) {
						Engage(member, actor.get(), controller, a_dt);
						controller = Game::Controller(actor.get());  // StartCombat may have made one
					}
				}
				if (!actor || !Game::Usable(actor.get()) || !actor->IsInCombat() || !controller) {
					member.desired = {};
					member.armed = false;
					member.warnedAmmo = false;
					member.rightHand = nullptr;
					member.drained = false;
					continue;
				}
				const auto* own = Party::FindOrders(member.id);
				if (!own) {
					continue;
				}
				const auto orders = *own;
				auto       resolved = orders.position;
				const bool down = actor->AsActorState()->IsBleedingOut();
				if (member.down && !down) {
					Log::Info("[Battle] {} is back up: {}, {}."sv, member.name, Info(orders.role).name, Info(resolved).name);
				}
				member.down = down;
				// An archer with nothing to shoot fights as a Fighter: a dagger kept for point
				// blank would lose to a bow that cannot fire anyway, and on Hold back they take
				// on only what comes near them instead of running across the field.
				auto fightAs = orders;
				if (!Loadouts::Locked(member.id) && orders.role == Role::kArcher && member.arsenalKnown && !member.arsenal.ammo) {
					fightAs.role = Role::kFighter;
					if (!member.warnedAmmo) OutOfArrows(member, KitOf(actor.get()));
				}
				// Out of magicka, a mage or healer falls back and lets it come back, wherever they were
				// told to stand.
				Drain(member, actor.get(), orders);
				const bool recovering = member.drained && ChoosesSpells(orders.role);
				if (recovering) {
					resolved = Position::kHoldBack;
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
				if (!Loadouts::Locked(member.id) && markLive && member.markRanged != Role::kCount && !(member.markRanged == Role::kMage && member.drained)) {
					fightAs.role = member.markRanged;
				} else if (!Loadouts::Locked(member.id) && !Rules::IsRanged(fightAs.role)) {
					// Nobody left that they can get to: a bow or spells, if they have them, beat
					// standing under a ledge.
					const bool nobody = !member.outOfReach.empty() && std::all_of(profiles.begin(), profiles.end(), [&](const auto& a_profile) {
						auto enemy = a_profile.handle.get();
						return !enemy || enemy->IsDead() || OutOfReach(member, a_profile.handle);
					});
					Role instead = Role::kCount;
					if (nobody && member.arsenal.ranged && member.arsenal.ammo) {
						instead = Role::kArcher;
					} else if (nobody && member.arsenal.casts && !member.drained) {
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
				// What Path of the Companion's abilities ask of this companion's fight, if it is installed.
				const auto* asked = AbilityLink::For(member.id);
				const auto  circle = asked && asked->radius > 0.0f ? asked->circle.get() : RE::NiPointer<RE::TESObjectREFR>{};
				const bool  pull = circle != nullptr;
				const float pullRadius = pull ? asked->radius : 0.0f;
                const bool returningToCircle = pull && Game::Distance(actor.get(), circle.get()) > pullRadius * 2.0f;
                if (pull) { resolved = returningToCircle || recovering ? Position::kHoldBack : Position::kGuard; markLive = false; member.chasing = {}; }
                const bool charge = markLive && !recovering && !pull && !Rules::IsRanged(fightAs.role);
				const bool casterRole = ChoosesSpells(orders.role);
                const bool spellsChosen = ChoosesSpells(fightAs.role) && Loadouts::Hands(member.id) != Hands{};
				auto tweaks = asked ? asked->tweaks : StyleTweaks{};
                tweaks.recovery = returningToCircle;
                tweaks.caster = casterRole;
                tweaks.magickaRecovery = recovering;
                Styles::Apply(actor.get(), fightAs, charge ? Position::kFrontLine : resolved, charge, spellsChosen, tweaks);
                if (pull) {
					// Hurt: into a healer's circle until they are well again.
					Leash::Apply(actor.get(), pullRadius, circle.get(), true);
					member.leashMoved = true;
				} else if (settings.guardLeash || member.leashMoved) {
					member.leashMoved = false;
					float leash = 0.0f;
					if (markLive && !recovering) {
						leash = 0.0f;
					} else if (resolved == Position::kGuard) {
						leash = settings.tuning.guardRadius;
					} else if (resolved == Position::kHoldBack) {
						leash = settings.holdBackLeash;
					} else if (resolved == Position::kWithMe) {
						// Within the distance set and no further: a blade at the edge still reaches
						// an enemy just past it, and a bow or a spell reaches anyone.
						leash = settings.tuning.withMeRadius;
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

				WatchHands(member, actor.get(), fightAs, resolved, controller);
				ReportHands(member, actor.get(), controller, orders);

				auto focus = orders.focus;
				if (focus == Focus::kMarked && !member.marked) {
					focus = Focus::kEngine;
				}
				const auto pick = Rules::PickTarget(views, focus, resolved, Rules::IsRanged(fightAs.role), settings.tuning);
				auto desired = pick.index >= 0 ? profiles[static_cast<std::size_t>(pick.index)].handle : RE::ActorHandle{};
				// Shield up against archers: turned to the nearest of them, closing under it.
				if (!pull && member.shielding && member.shieldFacing && !markLive && !OutOfReach(member, member.shieldFacing)) {
					desired = member.shieldFacing;
				}
				// Closing on someone in melee, shield up or not: if it never happens, they are out of
				// reach. Down, staggered or thrown about, nobody closes on anyone, so the clock waits:
				// counted, it left them sure they could reach no one once back on their feet.
				const bool reeling = down || actor->IsInRagdollState() || actor->AsActorState()->IsStaggered() || actor->IsInKillMove();
				if (reeling) {
					member.chasing = {};
				} else if (!pull && !Rules::IsRanged(fightAs.role) &&
						   (resolved == Position::kFree || resolved == Position::kFrontLine || markLive || member.shielding)) {
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
				// A volley: every archer and mage on the one enemy.
				if (asked && asked->target && !pull && (fightAs.role == Role::kArcher || fightAs.role == Role::kMage) && Overrides::Takes(controller, asked->target)) {
					desired = asked->target;
				}
				if (pull) {
                    auto target = desired.get();
                    if (returningToCircle || (target && Game::Distance(target.get(), circle.get()) > pullRadius * 2.0f)) desired = {};
                }
				member.desired = desired;
				// Caster roles avoid melee in automatic and manual modes; recovery
				// suspends attacks until their magicka reserve is ready again.
				entries.emplace_back(reinterpret_cast<std::uintptr_t>(controller), Game::Native(member.handle), Game::Native(desired), false,
					recovering, casterRole);
				// Their chosen spells in use, whatever kind they are: cast for them when the combat AI will not.
				if (!Loadouts::Locked(member.id) && !recovering && !returningToCircle && ChoosesSpells(fightAs.role)) {
					auto on = desired ? desired.get() : controller->targetHandle.get();
					if (!pull || (on && Game::Distance(on.get(), circle.get()) <= pullRadius * 2.0f))
                        Spells::Drive(member, actor.get(), on.get(), a_dt);
				}
			}

			// Enemies held by a taunt want the tank, whatever their own AI thinks, and press them
			// instead of circling at a distance waiting a turn.
			std::vector<RE::NiPointer<RE::Actor>> held;
			for (const auto& hold : AbilityLink::Holds()) {
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

		// Paths on its own: nobody is told whom to fight or where to stand. What is left is what
		// the abilities ask of a fight: their traits in how they fight, a healer's circle drawing the
		// hurt in, a volley's target, and enemies a taunt holds on the tank.
		void DecideAlone(float a_dt)
		{
			std::vector<Overrides::Entry> entries;
			for (auto& member : Party::Members()) {
				auto  actor = member.CanTakeRole() ? member.handle.get() : nullptr;
				auto* controller = actor ? Game::Controller(actor.get()) : nullptr;
				const auto* orders = Party::FindOrders(member.id);
				if (!actor || !Game::Usable(actor.get()) || !actor->IsInCombat() || !controller || !orders) {
					member.desired = {};
					continue;
				}
                Drain(member,actor.get(),*orders);
                const bool recovering=member.drained && ChoosesSpells(orders->role);
				member.down = actor->AsActorState()->IsBleedingOut();
				const auto pull = Circles::PullFor(actor.get());
                const bool returningToCircle = pull && Game::Distance(actor.get(), pull->centre) > pull->radius * 2.0f;
                auto tweaks = Arts::TweaksFor(member.id, PathsLink::KitOf(member.id, orders->role));
                tweaks.recovery = returningToCircle;
                tweaks.caster=ChoosesSpells(orders->role);tweaks.magickaRecovery=recovering;
                Styles::Apply(actor.get(), *orders, returningToCircle || recovering ? Position::kHoldBack : Position::kFree, false, false, tweaks);
                if (pull) {
					Leash::Apply(actor.get(), pull->radius, pull->centre, true);
					member.leashMoved = true;
				} else if (member.leashMoved) {
					Leash::Apply(actor.get(), 0.0f);
					member.leashMoved = false;
				}
				RE::ActorHandle desired;
				if (const auto volley = Specials::VolleyTarget();
					!pull && volley && (orders->role == Role::kArcher || orders->role == Role::kMage) && Overrides::Takes(controller, volley)) {
					desired = volley;
				}
				if (returningToCircle) desired = {};
				member.desired = desired;
                if (desired || ChoosesSpells(orders->role)) {
                    entries.emplace_back(reinterpret_cast<std::uintptr_t>(controller), Game::Native(member.handle), Game::Native(desired),false,recovering,ChoosesSpells(orders->role));
                }
			}

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

		// Path of the Companion beside Fight With Me: the role it gives each companion is the path
		// that counts, and whoever they were sent after is who their abilities go for.
		void Mirror()
		{
			for (auto& member : Party::Members()) {
				if (!member.CanTakeRole()) {
					continue;
				}
				member.marked = OrdersLink::MarkOf(member.id);
				const auto  role = OrdersLink::RoleOf(member.id);
				const auto* orders = Party::FindOrders(member.id);
				if (role && orders && orders->role != *role) {
					auto now = *orders;
					now.role = *role;
					Party::SetOrders(member.id, now);
					if (const auto* after = Party::FindOrders(member.id); after && after->role == *role) Log::Info("[Orders] {} follows the {} now, the role Fight With Me gives them."sv, member.name, Paths::Info(*role).title);
				}
			}
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
            if (!Party::CanHaveRole(a_id)) return false;
			const auto* before = Party::FindOrders(a_id);
			if (before && *before == a_orders) {
				return false;
			}
			const bool newRole = !before || before->role != a_orders.role;
			Party::SetOrders(a_id, a_orders);
			const auto* member = Party::Find(a_id);
			// In Path of the Companion a role is only which path counts, and their hands stay their own.
			if (auto actor = member ? member->handle.get() : nullptr; actor && newRole && Host::Orders()) {
				Spells::Recheck(actor.get(), a_orders.role);  // a mage turned healer keeps only what a healer may hold
				// A new role changes what they hold, there and then, not when a fight next asks.
				if (!InAFight()) {
					if (const auto armed = ArmFor(actor.get(), a_orders.role); !armed.empty()) {
						Log::Info("[Style] {} is a {} now: {} in hand."sv, member->name, Info(a_orders.role).name, armed);
					}
				}
			}
			const std::string position{ Info(a_orders.position).name };
			std::string ability;
			if (Host::Abilities() && (a_orders.role == Role::kTank || a_orders.role == Role::kHealer)) {
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
				if (const auto* own = !member.CanTakeRole() ? nullptr : Party::FindOrders(member.id)) {
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
			Notices::Push(std::format("{}  {}", Tr(info.name), Tr(info.summary)), Notices::Kind::kOrder);
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
                if (command.member && (!member || !member->CanTakeRole() || !Party::CanHaveRole(command.member))) {
                    if (member) Party::Answer(*member, Tr("Roles are unavailable for creature companions and summons."));
                    continue;
                }
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
					// Roles are settled before a fight; in one, only focus, position and the switches change.
					if (const auto* own = Party::FindOrders(command.member); own && own->role != command.orders.role && InAFight()) {
						Log::Info("[Party] {} refused for {:08X}: roles are set out of a fight."sv, Info(command.orders.role).name, command.member);
						if (member) {
							Party::Answer(*member, Tr("Roles are set out of a fight."));
						}
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
						member->markAway = 0.0f;
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
				case Command::Type::kRejuvenateNow:
					if (member) {
						member->rejuvenateRequested = true;
						member->askedAt = ::GetTickCount64();
					}
					break;
				case Command::Type::kShout:
					GiveOrder(command.shout);
					break;
				case Command::Type::kSetSpell:
					if (member) {
						auto        actor = member->handle.get();
						const auto* own = Party::FindOrders(command.member);
						if (!actor || !own || !member->CanTakeRole() || command.hand>2 || (command.hand<2 && !ChoosesSpells(own->role))) {
							Party::Answer(*member, Tr("Only mages and healers have spells chosen for them."));
						} else if (fighting || actor->IsInCombat()) {
							Party::Answer(*member, Tr("Spells can only be changed out of a fight."));
						} else {
							const auto outcome = Loadouts::Toggle(actor.get(), own->role, command.hand, command.spell);
							Party::Answer(*member, outcome.text, outcome.done);
							slowParts.erase(command.member);  // the sheet shows it at once
						}
					}
					break;
				case Command::Type::kAbilityNow:
					if (member) {
						if (PathsLink::Unlimited(member->id)) {
							const auto* orders=Party::FindOrders(member->id);
							if (orders) {
								const auto kit=PathsLink::KitOf(member->id,orders->role);
								if (kit.Carries(command.ability)) Abilities::DebugActivate(*member,orders->role,command.ability,kit.Rank(command.ability));
							}
							break;
						}
						member->askedAt = ::GetTickCount64();
						switch (command.ability) {
						case Paths::Ability::kTaunt:
							member->tauntRequested = true;
							break;
						case Paths::Ability::kHeal:
							member->healRequested = true;
							break;
						case Paths::Ability::kRejuvenate:
							member->rejuvenateRequested = true;
							break;
						default:
							if (Paths::SpecialFor(command.ability) != Special::kNone) {
								member->specialRequested = true;
							} else {
								Arts::Request(member->id, command.ability);
							}
							break;
						}
					}
					break;
				case Command::Type::kLearn:
				case Command::Type::kForget:
				case Command::Type::kResetPath:
				case Command::Type::kEquip:
				case Command::Type::kInvest:
				case Command::Type::kDivest:
					if (member) {
						// Ranks and bonus points can be bought in a fight; carrying and giving back wait
						// for it to end, as roles and spells do.
						const bool buying = command.type == Command::Type::kLearn || command.type == Command::Type::kInvest;
						if (!buying && (fighting || InAFight())) {
							Party::Answer(*member, Tr("Paths are changed out of a fight."));
							break;
						}
						PathsLink::Outcome outcome;
						switch (command.type) {
						case Command::Type::kLearn:
							outcome = PathsLink::Learn(member->id, command.path, command.node);
							break;
						case Command::Type::kForget:
							outcome = PathsLink::Forget(member->id, command.path, command.node);
							break;
						case Command::Type::kResetPath:
							outcome = PathsLink::Reset(member->id, command.path);
							break;
						case Command::Type::kInvest:
							{
								// What a point is worth is ours to say: the add-on is told how many more still count.
								const auto* progress = PathsLink::Find(member->id);
								const int   on = progress ? Paths::Spent(progress->record, command.path) : 0;
								outcome = PathsLink::Invest(member->id, command.path, Rules::PathBonusRoom(command.path, on, Settings::Get().tuning));
							}
							break;
						case Command::Type::kDivest:
							outcome = PathsLink::Divest(member->id, command.path);
							break;
						default:
							outcome = PathsLink::Equip(member->id, command.path, command.slot, command.ability);
							break;
						}
						Party::Answer(*member, outcome.text, outcome.done);
						slowParts.erase(command.member);
					}
					break;
				case Command::Type::kDebugAbility:
					if (member) Abilities::DebugActivate(*member, command.path, command.ability, command.rank);
					break;
				case Command::Type::kUnlimited:
					if (member) {
						// Taking them back changes what they carry, so that waits for the fight to end.
						if (!command.on && (fighting || InAFight())) {
							Party::Answer(*member, Tr("Turn Debug Mode off after the fight."));
							break;
						}
						const auto outcome = PathsLink::SetUnlimited(member->id, command.on);
						Party::Answer(*member, outcome.text, outcome.done);
						if (outcome.done) {
							Notices::Push(outcome.text, Notices::Kind::kInfo);
							Log::Info("[Paths] {} {}."sv, member->name, command.on ? "has Debug Mode enabled" : "is back to earned points");
						}
						slowParts.erase(command.member);
					}
					break;
				case Command::Type::kAutoBattle:
					if (Settings::Get().autoBattle != command.on) {
						auto data = Settings::Chosen();
						data.autoBattle = command.on;
						Settings::Apply(data);
						Log::Info("[Orders] Auto battle {}."sv, command.on ? "on: a companion who sees an enemy starts the fight" : "off: they wait for the fight to come to them");
						Notices::Push(command.on ? Tr("Auto battle on: whoever sees an enemy starts the fight.") : Tr("Auto battle off: they wait for the fight to come to them."),
							Notices::Kind::kOrder);
					}
					break;
				case Command::Type::kPreset:
					for (auto& each : Party::Members()) {
						each.outOfReach.clear();
						each.reachRole = Role::kCount;
						if (const auto* orders = !each.CanTakeRole() ? nullptr : Party::FindOrders(each.id)) {
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
				{ RE::ActorValue::kOneHanded, Ntr("One-Handed") },
				{ RE::ActorValue::kTwoHanded, Ntr("Two-Handed") },
				{ RE::ActorValue::kArchery, Ntr("Archery") },
				{ RE::ActorValue::kBlock, Ntr("Block") },
				{ RE::ActorValue::kHeavyArmor, Ntr("Heavy Armor") },
				{ RE::ActorValue::kLightArmor, Ntr("Light Armor") },
				{ RE::ActorValue::kSneak, Ntr("Sneak") },
				{ RE::ActorValue::kDestruction, Ntr("Destruction") },
				{ RE::ActorValue::kRestoration, Ntr("Restoration") },
				{ RE::ActorValue::kConjuration, Ntr("Conjuration") },
				{ RE::ActorValue::kAlteration, Ntr("Alteration") },
				{ RE::ActorValue::kIllusion, Ntr("Illusion") },
			} };
			for (const auto& [value, name] : combat) {
				// The game's own name for the skill, in the player's language; ours if it has none.
				const auto* info = RE::ActorValueList::GetActorValueInfo(value);
				const char* own = info ? info->GetFullName() : nullptr;
				a_member.skills.emplace_back(own && *own ? std::string(own) : std::string(Tr(name)), static_cast<int>(Game::Current(a_actor, value)));
			}
			std::stable_sort(a_member.skills.begin(), a_member.skills.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			a_member.skills.resize(std::min<std::size_t>(a_member.skills.size(), 6));
		}

		void FillSlowParts(View::Member& a_member, RE::Actor* a_actor, std::uint64_t a_now)
		{
			auto& cached = slowParts[a_member.id];
			if (cached.at == 0 || a_now - cached.at >= kSlowMs || cached.view.orders.role != a_member.orders.role) {
				View::Member fresh;
				fresh.orders.role = a_member.orders.role;
				Sheet(fresh, a_actor);
                fresh.pools=Loadouts::Of(a_member.id);
                if(!fighting) fresh.weaponOptions=Loadouts::Weapons(a_actor,a_member.orders.role);
				// Spells for the hands: what is chosen, and, out of a fight, what could be.
				if (ChoosesSpells(a_member.orders.role)) {
					const auto hands = Loadouts::Hands(a_member.id);
					for (const auto hand : { kLeftHand, kRightHand }) {
						fresh.hands[hand] = Spells::Describe(hands[hand], a_actor);
					}
					if (!fighting) {
						fresh.spellOptions = Spells::OptionsFor(a_actor, a_member.orders.role);
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
			a_member.hands = s.hands;
			a_member.spellOptions = s.spellOptions;
            a_member.weaponOptions=s.weaponOptions;
            a_member.pools=s.pools;
		}

		// What the party's own abilities have put on a companion for a while, good or bad. Only
		// these: the game's own effects (scripted mod effects, skill fortifies and the like) are
		// too many and mostly noise in a fight.
		void Statuses(View::Member& a_view, const Party::Member& a_member, RE::Actor* a_actor)
		{
			using Tint = View::Status::Tint;
			auto& out = a_view.statuses;
			// A taunt's armor and Rejuvenate, given with or without a path (paths tag theirs).
			auto either = [&](RE::ActorValue a_value, Paths::Perk a_perk) {
				auto on = Abilities::BoostOn(a_member.id, a_value, 0);
				return on ? on : Abilities::BoostOn(a_member.id, a_value, static_cast<std::uint16_t>(a_perk));
			};
			if (Host::Abilities()) {
				if (const auto brace = either(RE::ActorValue::kDamageResist, Paths::Perk::kTauntBrace)) {
					out.push_back({ std::string(Tr("Braced")), Trf("+{:.0f} armor from their taunt", brace->amount), 0xF3ED, Tint::kArmor, brace->left, brace->length });
				}
				if (const auto quick = RejuvenateField::On(a_actor); quick.left > 0) {
					out.push_back(
						{ std::string(Tr("Rejuvenated")), Trf("Restoring {:g}% magicka per second, up to {:.0f}, inside the circle",RejuvenatePolicy::Rate(quick.rank)*100,RejuvenatePolicy::Cap(quick.rank)), 0xF5BB, Tint::kMagicka, quick.left, quick.length });
				}
                if(const auto moon=AzurasMoon::On(a_actor);moon.left>0) {
                    out.push_back({std::string(Tr("Azura's Blessing")),Trf("{:.0f}% chance to ignore hostile damage",moon.chance*100),0xF186,Tint::kMagicka,moon.left,moon.length});
                }
				Arts::Statuses(a_member.id, out);
			}
			if (a_member.drained) {
				// The ring fills as the magicka comes back, full when they return to their place.
				const float cost = CastCost(a_member, a_actor);
				const float most = Game::Maximum(a_actor, RE::ActorValue::kMagicka);
				const float back = std::min(std::max(cost * 2.0f, most / 3.0f), most);
				View::Status dry{ std::string(Tr("Out of magicka")), std::string(Tr("Falling back until it comes back")), 0xF5C7, Tint::kDebuff };
				dry.progress = back > 0.0f ? std::clamp(Game::Current(a_actor, RE::ActorValue::kMagicka) / back, 0.0f, 1.0f) : 0.0f;
				out.push_back(std::move(dry));
			}
		}

		void BuildView()
		{
			ApplyCommands();
			if (planOpen.load()) {
				TimeBend::KeepClear();  // the plan asks for a view every frame, so this runs even with the world frozen
			}

			auto  snapshot = std::make_shared<View::Snapshot>();
			auto* player = RE::PlayerCharacter::GetSingleton();
			snapshot->serial = ++viewSerial;
			snapshot->tuning = Settings::Get().tuning;
			snapshot->walkWithMe = WalkWithMe::Connected();
			snapshot->shout = shout;
			snapshot->autoBattle = Settings::Get().autoBattle;
			snapshot->drawOnSight = Settings::Get().drawOnSight;
			snapshot->paths = PathsLink::Active();
			snapshot->pathsAlone = Host::PathsAlone();
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
			if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
				if (auto ref = pick->GetActiveTarget().get(); ref && Party::Find(ref->GetFormID())) {
					snapshot->aimed = ref->GetFormID();
				}
			}
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
                m.creature = member.creature;
				m.walkWithMe = member.managedByWalkWithMe;
				m.arsenal = member.arsenal;
				m.arsenalKnown = member.arsenalKnown;
				if (const auto* orders = Party::FindOrders(member.id)) {
					m.orders = *orders;
				}
				m.resolved = m.orders.position;
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
				m.asked = member.tauntRequested || member.healRequested || member.rejuvenateRequested || member.specialRequested;
				if (member.answerAt && now - member.answerAt < 6000) {
					m.answer = member.answer;
					m.answerDone = member.answerDone;
					m.answerAge = static_cast<float>(now - member.answerAt) / 1000.0f;
				}
				m.held = member.held;
				if (Host::Abilities() && member.CanTakeRole()) {
					const auto taunt = Abilities::PreviewTaunt(actor.get());
					m.tauntRadius = taunt.radius;
					m.tauntCertain = taunt.certainUpTo;
					m.tauntNever = taunt.noChanceFrom;
					m.tauntHold = taunt.hold;
					m.tauntStamina = taunt.stamina;
					m.tauntArmor = taunt.armor;
					m.tauntArmorSeconds = taunt.armorSeconds;
					m.healReady = member.healReady;
					m.healAmount = Rules::TeamHealAmount(m.level, snapshot->tuning);
					m.rejuvenateReady = member.rejuvenateReady;
					m.rejuvenateCooldown = member.rejuvenateCooldown;
					m.rejuvenateSeconds = Rules::RejuvenateSeconds(m.level, snapshot->tuning);
					m.special = SpecialOf(m.orders.role);
					m.specialReady = member.specialReady;
					m.specialCooldown = member.specialCooldown;
					m.guarding = member.guardFor > 0.0f;
					if (const auto progress = Leveling::Find(member.id)) {
						m.leveling = true;
						m.xp = progress->xp;
						m.xpNext = progress->next;
						m.levelUpAge = progress->sinceLevelUp;
						m.levelCapped = progress->capped;
					}
					if (const auto* profile = Traits::Find(member.id)) {
						m.traits = profile->traits;
						m.rarity = profile->rarity;
					}
					if (m.orders.role == Role::kTank) {
						m.ring = GroundRing(actor->GetPosition(), taunt.radius, 40);
					}
					// The Paths add-on: every path, and the two abilities carried on this one.
					if (const auto* progress = PathsLink::Find(member.id)) {
						m.paths = true;
						m.record = progress->record;
						m.pointsEarned = progress->earned;
						m.pointsSpent = progress->spent;
						m.pointsAbove = PathsLink::PointsAboveLevel();
						m.respec = PathsLink::RespecAllowed();
						m.unlimited = progress->unlimited;
						m.unlimitedAllowed = PathsLink::UnlimitedAllowed();
						const auto kit = PathsLink::KitOf(member.id, m.orders.role);
						for (std::size_t s = 0; s < Paths::kSlots; ++s) {
							auto&      carried = m.carried[s];
							const auto ability = kit.slots[s];
							carried.ability = ability;
							switch (ability) {
							case Paths::Ability::kTaunt:
								carried.ready = member.tauntReady;
								carried.cooldown = member.tauntCooldown > 0.0f ? member.tauntCooldown : kit.Cooldown(ability);
								break;
							case Paths::Ability::kHeal:
								carried.ready = member.healReady;
								carried.cooldown = kit.Cooldown(ability);
								break;
							case Paths::Ability::kRejuvenate:
								carried.ready = member.rejuvenateReady;
								carried.cooldown = member.rejuvenateCooldown > 0.0f ? member.rejuvenateCooldown : kit.Cooldown(ability);
								break;
							default:
								if (Paths::SpecialFor(ability) != Special::kNone) {
									carried.ready = member.specialReady;
									carried.cooldown = member.specialCooldown > 0.0f ? member.specialCooldown : kit.Cooldown(ability);
								} else {
									const auto clock = Arts::ClockOf(member.id, ability);
									carried.ready = clock.ready;
									carried.cooldown = clock.cooldown > 0.0f ? clock.cooldown : kit.Cooldown(ability);
								}
								break;
							}
						}
					}
					m.asked = m.asked || Arts::Asked(member.id);
				}
				m.head = Screen(Game::HeadPosition(actor.get()) + RE::NiPoint3{ 0.0f, 0.0f, 22.0f });
				if (snapshot->overhead) {
					Footprint(actor.get(), m.foot, m.footRing);
				}
				FillSlowParts(m, actor.get(), now);
				// The vitals as they stand this frame, not as the sheet last read them.
				m.healthNow = Game::Current(actor.get(), RE::ActorValue::kHealth);
				m.healthMax = Game::Maximum(actor.get(), RE::ActorValue::kHealth);
				m.magickaNow = Game::Current(actor.get(), RE::ActorValue::kMagicka);
				m.magickaMax = Game::Maximum(actor.get(), RE::ActorValue::kMagicka);
				m.staminaNow = Game::Current(actor.get(), RE::ActorValue::kStamina);
				m.staminaMax = Game::Maximum(actor.get(), RE::ActorValue::kStamina);
				m.drained = member.drained;
				if (Host::Abilities()) {
					m.rejuvenated = Abilities::RejuvenatedFor(member.id);
				}
				if (member.CanTakeRole()) {
					Statuses(m, member, actor.get());
				}
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
				// Read now, not on the planner's last tick: with time slowed that tick is seconds apart.
				e.healthNow = Game::Current(actor.get(), RE::ActorValue::kHealth);
				e.healthMax = Game::Maximum(actor.get(), RE::ActorValue::kHealth);
				e.magickaNow = Game::Current(actor.get(), RE::ActorValue::kMagicka);
				e.magickaMax = Game::Maximum(actor.get(), RE::ActorValue::kMagicka);
				e.staminaNow = Game::Current(actor.get(), RE::ActorValue::kStamina);
				e.staminaMax = Game::Maximum(actor.get(), RE::ActorValue::kStamina);
				e.health = Game::Fraction(actor.get(), RE::ActorValue::kHealth);
				e.magicka = Game::Fraction(actor.get(), RE::ActorValue::kMagicka);
				e.stamina = Game::Fraction(actor.get(), RE::ActorValue::kStamina);
				e.caster = profile.caster;
				e.healer = profile.healer;
				e.archer = profile.archer;
				e.boss = profile.boss;
				if (auto target = profile.target.get()) {
					e.target = target->GetFormID();
				}
				e.targetsPlayer = profile.target == playerHandle;
				if (Host::Abilities()) {
					if (auto tank = Abilities::HolderOf(profile.handle).get()) {
						e.heldBy = tank->GetFormID();
					}
				} else {
					for (const auto& hold : AbilityLink::Holds()) {
						if (auto tank = hold.tank.get(); tank && hold.enemy == profile.handle) {
							e.heldBy = tank->GetFormID();
						}
					}
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
		// Fight With Me: battle orders. Path of the Companion: abilities, traits, levels and path
		// bonuses, and on its own also the steering its abilities need.
		constexpr bool orders = Host::Orders();
		constexpr bool abilities = Host::Abilities();
		TimeBend::Update();
		if (abilities) {
			Effects::Animate();
		}
		ApplyCommands();
		const float cooled = Party::Cool(a_dt);
		if (abilities) {
			Arts::Cool(cooled);
			AbilityAuto::Tick(cooled);  // cooldowns run on world time, fight or not
		}
		// A taunt's armor and Rejuvenate run out on time, fight or not. In Fight With Me the only
		// lifts ever here are those an older version left on in a save, to come off.
		Abilities::UpdateBoosts(a_dt);
		glowIn -= a_dt;
		if (glowIn <= 0.0f) {
			glowIn = 0.2f;
			if (abilities) {
				Effects::Update();
				ShowAbilities();
			}
		}
		if (!Settings::Get().enabled) {
			ArcherAmmo::Clear();
			partyFighting = false;
			Overrides::PublishHands({});
			PathBonus::Clear();
			ClearLooks();
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
			if (orders) {
				for (const auto& member : Party::Members()) {
					if (member.CanTakeRole()) {
						const auto* own = Party::FindOrders(member.id);
						auto actor = member.handle.get();
						if (actor && own) {
							Loadouts::Recheck(actor.get(), own->role);
						}
					}
				}
				ForgetStaleMarks();
			}
			if (abilities) {
				if (!Host::PathsAlone()) {
					Mirror();
				}
				Traits::Refresh();
				Leveling::Refresh();
				PathsLink::Refresh();
				PathBonus::Publish();
			} else {
				Leveling::Refresh();  // without Path of the Companion: an older save's levels go back to their own
			}
			rosterIn = 0.5f;
		}
		if (Host::Steers()) {
			PublishHands();  // cheap when nothing changed
		}

		if (abilities) {
			ArcherAmmo::Refresh(a_dt);
			// The effects play out even when their blow ended the fight, or they would freeze where they were.
			Circles::Tick(a_dt);
			Blasts::Tick(a_dt);
			DragonBreaths::Tick(a_dt);
			Orbs::Tick(a_dt);
			Quakes::Tick(a_dt);
			Rainfall::Tick(a_dt);
			Cloaks::Tick(a_dt);
			TauntBears::Tick(a_dt);
			ChaosPulses::Tick(a_dt);
			Rejuvenations::Tick(a_dt);
			Haunts::Tick(a_dt);
			Sunders::Tick(a_dt);
			Disarms::Tick(a_dt);
            RiposteVfx::Tick(a_dt);
            ArrowArts::Tick(a_dt);
            Windsteps::Tick(a_dt);
			SecondWinds::Tick(a_dt);
            AzurasMoon::Tick(a_dt);
			Berserks::Tick(a_dt);
            Surges::Tick(a_dt);
			TankActs::Tick(a_dt);
			WeakWills::Tick(a_dt);
			Bastions::Tick(a_dt);
			EffectAudio::Tick(a_dt);
			Freeze::Tick(a_dt);
		}
		const bool anyone = AnyoneFighting(player);
		partyFighting = anyone && (std::any_of(Party::Members().begin(), Party::Members().end(), [](const Party::Member& a_member) {
			auto actor = a_member.summon ? nullptr : a_member.handle.get();
			return actor && actor->IsInCombat();
		}) || !Enemies::All().empty());
		if (abilities) {
			Leveling::Update(anyone);  // before a fight's end is noticed, so its last enemy is paid out
		}
		if (!anyone) {
            if (abilities) PartyBalance::Clear();
			if (fighting) {
				StandDown();
				if (abilities) {
					Leveling::FightOver();
				}
				AbilityLink::Clear();
				fighting = false;
				Log::Info("[Battle] Combat over."sv);
			}
			if (abilities) {
				Arts::Tick(a_dt);
				Specials::Tick(a_dt);
			} else {
				Sight::Update(a_dt);
			}
			return;
		}
		if (!fighting) {
			fighting = true;
			if (orders) {
				Sight::FightStarted();
			}
			decideIn = 0.0f;
			Party::Refresh();
			if (abilities) {
                Enemies::Update(0);
                PartyBalance::Update(0);
				PathBonus::Publish();
			}
			Log::Info("[Battle] Combat with {} companion(s)."sv, Party::Members().size());
		}

		if (abilities) {
			// Arrows cross a room in under a second: the shield is checked every frame, not on the planner's tick.
			Abilities::BlockIncoming(a_dt);
			Specials::Tick(a_dt);
			Arts::Tick(a_dt);
			Traits::Tick(a_dt);
		}

		// Someone just brought into the fight gets the plan's pick at once: a scripted opener (a blink
		// behind the enemy, say) reads their target in its first moments.
		{
			std::vector<RE::FormID> now;
			for (const auto& member : Party::Members()) {
				if (auto actor = !member.CanTakeRole() ? nullptr : member.handle.get(); actor && actor->IsInCombat()) {
					now.push_back(member.id);
					if (std::find(wereFighting.begin(), wereFighting.end(), member.id) == wereFighting.end()) {
						decideIn = 0.0f;
					}
				}
			}
			wereFighting = std::move(now);
		}

		pendingDt += a_dt;
		decideIn -= a_dt;
		if (decideIn <= 0.0f) {
			Enemies::Update(pendingDt);
            if (abilities) PartyBalance::Update(pendingDt);
			if (abilities) {
				if (!Host::PathsAlone()) {
					Mirror();  // marks change mid-fight, and abilities aim at them
				}
				Abilities::Update(pendingDt);
				Specials::Update(pendingDt);
				Arts::Update();
				PathBonus::Fight();
				if (Host::PathsAlone()) {
					DecideAlone(pendingDt);
				}
			} else {
				AbilityLink::Fetch();
				Decide(player, pendingDt);
				GlowMarks(pendingDt);
				LeaveIfEmpty(pendingDt);
			}
			pendingDt = 0.0f;
			decideIn = Settings::Get().planInterval;
		}
		// After the enemy list is fresh: whoever the combat AI has left out joins in.
		if (orders) {
			Sight::Join(a_dt);
		}

		statsIn -= a_dt;
		if (statsIn <= 0.0f && Host::Steers()) {
			statsIn = 30.0f;
			const auto stats = Overrides::TakeStats();
			const auto bonus = PathBonus::TakeStats();
			Log::Info("[Hook] {} selector calls, {} ours, {} redirected, {} refused by the engine's test{}{}{}{}{}{}{}{}."sv,
				stats.calls, stats.matched, stats.overridden, stats.rejected,
				stats.bound ? std::format("; bound weapons refused {} times", stats.bound) : std::string(),
				stats.spells ? std::format("; other magic kept out of chosen hands {} times", stats.spells) : std::string(),
				stats.rested ? std::format("; weapons kept from resting casters {} times", stats.rested) : std::string(),
				stats.melee ? std::format("; weapons kept from mages and healers {} times", stats.melee) : std::string(),
				stats.skill ? std::format("; chosen spells let past the skill check {} times", stats.skill) : std::string(),
				bonus.heals ? std::format("; healers' healing strengthened {} times", bonus.heals) : std::string(),
				bonus.costs ? std::format("; mages' spell costs cut {} times", bonus.costs) : std::string(),
				bonus.blows || bonus.harms ? std::format("; damage to tanks cut on {} blows and {} harmful spells", bonus.blows, bonus.harms) : std::string());
		}
	}

	void Reset()
	{
		StandDown();
		ClearLooks();
		Abilities::EndBoosts();
		Effects::Clear();
		Sight::Reset();
		Traits::Reset();
		Leveling::Reset();
		Arts::Reset();
		PathBonus::Clear();
		PathsLink::Clear();
		AbilityLink::Clear();
		fighting = false;
		partyFighting = false;
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
		// Unique traits and levels earned by fighting are Path of the Companion's, like the abilities.
		if (Host::Abilities()) {
			Arts::InstallEvents();
			Traits::InstallEvents();
			Leveling::InstallEvents();
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

	bool PartyFighting() noexcept
	{
		return partyFighting.load();
	}

	bool PlanOpen() noexcept
	{
		return planOpen.load();
	}

	void PlanWindowClosed(bool a_menuStillOpen)
	{
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask([a_menuStillOpen] { TimeBend::HandBack(a_menuStillOpen); });
		}
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

	std::vector<Steer> Steering()
	{
		std::vector<Steer> out;
		for (const auto& member : Party::Members()) {
			auto        actor = member.CanTakeRole() ? member.handle.get() : nullptr;
			const auto* own = Party::FindOrders(member.id);
			if (!actor || !Game::Usable(actor.get()) || !actor->IsInCombat() || !own) {
				continue;
			}
			Steer steer;
			steer.member = member.id;
			if (const auto pull = Circles::PullFor(actor.get()); pull && pull->centre) {
				steer.circle = pull->centre->GetHandle();
				steer.radius = pull->radius;
			}
			steer.tweaks = Arts::TweaksFor(member.id, PathsLink::KitOf(member.id, own->role));
			if (const auto volley = Specials::VolleyTarget(); volley && steer.radius <= 0.0f && (own->role == Role::kArcher || own->role == Role::kMage)) {
				steer.target = volley;
			}
			if (steer.target || steer.radius > 0.0f || !(steer.tweaks == StyleTweaks{})) {
				out.push_back(steer);
			}
		}
		return out;
	}

	std::vector<std::pair<RE::ActorHandle, RE::ActorHandle>> Holds()
	{
		std::vector<std::pair<RE::ActorHandle, RE::ActorHandle>> out;
		for (const auto& hold : Abilities::Holds()) {
			out.emplace_back(hold.enemy, hold.tank);
		}
		return out;
	}

	RE::ActorHandle MarkOf(RE::FormID a_member)
	{
		const auto* member = Party::Find(a_member);
		return member ? member->marked : RE::ActorHandle{};
	}
}

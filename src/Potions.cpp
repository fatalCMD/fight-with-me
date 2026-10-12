#include "FWM/Potions.h"

#include "FWM/Abilities.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"

namespace FWM::Potions
{
	namespace
	{
		enum class Kind : std::uint8_t
		{
			kNone,
			kHealing,
			kMagicka,
			kStamina,
			kFortify,
			kResist
		};

		std::mt19937 rng{ std::random_device{}() };

		float Roll(float a_min, float a_max) { return std::uniform_real_distribution<float>(a_min, a_max)(rng); }

		bool IsResist(RE::ActorValue a_value)
		{
			switch (a_value) {
			case RE::ActorValue::kResistFire:
			case RE::ActorValue::kResistFrost:
			case RE::ActorValue::kResistShock:
			case RE::ActorValue::kResistMagic:
			case RE::ActorValue::kPoisonResist:
			case RE::ActorValue::kResistDisease:
				return true;
			default:
				return false;
			}
		}

		// What a potion is for, from its strongest effect. Poisons and food are not potions.
		Kind KindOf(const RE::AlchemyItem* a_item)
		{
			if (!a_item || a_item->IsPoison() || a_item->IsFood()) {
				return Kind::kNone;
			}
			const auto* effect = a_item->GetCostliestEffectItem();
			const auto* base = effect ? effect->baseEffect : nullptr;
			if (!base || base->IsDetrimental()) {
				return Kind::kNone;
			}
			const auto value = base->data.primaryAV;
			if (IsResist(value)) {
				return Kind::kResist;
			}
			switch (base->GetArchetype()) {
			case RE::EffectSetting::Archetype::kValueModifier:
				switch (value) {
				case RE::ActorValue::kHealth:
					return Kind::kHealing;
				case RE::ActorValue::kMagicka:
					return Kind::kMagicka;
				case RE::ActorValue::kStamina:
					return Kind::kStamina;
				default:
					return Kind::kFortify;  // regeneration, skills
				}
			case RE::EffectSetting::Archetype::kPeakValueModifier:
				return Kind::kFortify;
			default:
				return Kind::kNone;
			}
		}

		// The strongest potion of a kind they carry.
		RE::AlchemyItem* Strongest(RE::Actor* a_actor, Kind a_kind)
		{
			RE::AlchemyItem* best = nullptr;
			float            bestStrength = -1.0f;
			const auto       items = a_actor->GetInventory([](RE::TESBoundObject& a_object) { return a_object.Is(RE::FormType::AlchemyItem); });
			for (const auto& [object, entry] : items) {
				auto* potion = object ? object->As<RE::AlchemyItem>() : nullptr;
				if (!potion || entry.first <= 0 || KindOf(potion) != a_kind) {
					continue;
				}
				const auto* effect = potion->GetCostliestEffectItem();
				const float strength = effect ? effect->GetMagnitude() * std::max(static_cast<float>(effect->GetDuration()), 1.0f) : 0.0f;
				if (strength > bestStrength) {
					bestStrength = strength;
					best = potion;
				}
			}
			return best;
		}

		bool Drink(Party::Member& a_member, RE::Actor* a_actor, RE::AlchemyItem* a_potion, std::string_view a_why)
		{
			if (!a_potion) {
				return false;
			}
			Abilities::PlayIdle(a_actor, Forms::Get().idleDrink);
			if (!a_actor->DrinkPotion(a_potion, nullptr)) {
				Log::Info("[Potions] {} could not drink {}."sv, a_member.name, a_potion->GetName());
				a_member.drinkReady = 3.0f;
				return false;
			}
			a_member.drinkReady = 8.0f;
			Notices::Push(std::format("{} drinks {}.", a_member.name, a_potion->GetName()), Notices::Kind::kHeal);
			Log::Info("[Potions] {} drinks {} ({})."sv, a_member.name, a_potion->GetName(), a_why);
			return true;
		}

		bool Caster(Role a_role) { return a_role == Role::kMage || a_role == Role::kHealer; }

		// What they are short of, if anything.
		Kind Need(RE::Actor* a_actor, const Orders& a_orders)
		{
			const auto stock = StockOf(a_actor);
			if (stock.healing < 3) {
				return Kind::kHealing;
			}
			if (Caster(a_orders.role) && stock.magicka < 2) {
				return Kind::kMagicka;
			}
			if (!Caster(a_orders.role) && stock.stamina < 2) {
				return Kind::kStamina;
			}
			return Kind::kNone;
		}

		// An alchemy table they are already at does the work; nothing to mime.
		bool AtStation(RE::Actor* a_actor)
		{
			auto furniture = a_actor->GetOccupiedFurniture().get();
			auto* base = furniture ? furniture->GetBaseObject() : nullptr;
			auto* bench = base ? base->As<RE::TESFurniture>() : nullptr;
			if (!bench) {
				return false;
			}
			const auto type = bench->workBenchData.benchType.get();
			return type == RE::TESFurniture::WorkBenchData::BenchType::kAlchemy || type == RE::TESFurniture::WorkBenchData::BenchType::kAlchemyExperiment;
		}

		template <std::size_t N>
		RE::AlchemyItem* Grade(const std::array<RE::AlchemyItem*, N>& a_list, int a_tier)
		{
			for (int i = std::clamp(a_tier, 0, static_cast<int>(N) - 1); i >= 0; --i) {
				if (a_list[static_cast<std::size_t>(i)]) {
					return a_list[static_cast<std::size_t>(i)];
				}
			}
			return nullptr;
		}

		void StartBrew(Party::Member& a_member, RE::Actor* a_actor)
		{
			a_member.brewing = 0.0f;
			a_member.brewTakes = Roll(7.0f, 11.0f);
			const bool atStation = AtStation(a_actor);
			a_member.brewAnimated = !atStation && Settings::Get().brewAnimation && Abilities::PlayIdle(a_actor, Forms::Get().idleBrew);
			Log::Info("[Potions] {} starts brewing ({})."sv, a_member.name,
				atStation ? "at an alchemy table"sv : (a_member.brewAnimated ? "mixing in their hands"sv : "no animation"sv));
		}

		void EndAnimation(Party::Member& a_member, RE::Actor* a_actor)
		{
			if (a_member.brewAnimated) {
				Abilities::PlayIdle(a_actor, Forms::Get().idleBrewDone);
				a_member.brewExit = 2.0f;  // and make sure, in a moment, that the hands are free
			}
			a_member.brewAnimated = false;
			a_member.brewing = -1.0f;
		}

		void StopBrew(Party::Member& a_member, RE::Actor* a_actor, std::string_view a_why)
		{
			EndAnimation(a_member, a_actor);
			a_member.brewIn = 30.0f;
			Log::Info("[Potions] {} stops brewing: {}."sv, a_member.name, a_why);
		}

		void FinishBrew(Party::Member& a_member, RE::Actor* a_actor, const Orders& a_orders)
		{
			EndAnimation(a_member, a_actor);
			const auto& forms = Forms::Get();
			const int   level = a_actor->GetLevel();

			// What they were short of, in the grade their level brews: Minor at first, Ultimate
			// past fifty.
			const auto       need = Need(a_actor, a_orders);
			const int        tier = Rules::PotionTier(level, 1, 10, 6);
			RE::AlchemyItem* main = need == Kind::kMagicka ? Grade(forms.magickaPotions, tier) :
			                        (need == Kind::kStamina ? Grade(forms.staminaPotions, tier) : Grade(forms.healingPotions, tier));
			const int        count = 1 + (level >= 25 ? 1 : 0) + (level >= 40 && Roll(0.0f, 1.0f) < 0.5f ? 1 : 0);

			// Sometimes something stranger, the higher they are.
			RE::AlchemyItem* extra = nullptr;
			const float      r = Roll(0.0f, 1.0f);
			if (level >= 45 && r < 0.12f) {
				extra = Grade(forms.fortifyHealRate, Rules::PotionTier(level, 45, 5, 5));
			} else if (level >= 35 && r < 0.3f) {
				extra = Grade(forms.resistMagic, Rules::PotionTier(level, 35, 10, 4));
			} else if (level >= 25 && r < 0.5f) {
				const int grade = Rules::PotionTier(level, 25, 10, 5);
				extra = Caster(a_orders.role) ? Grade(forms.fortifyMagicka, grade) :
				        (a_orders.role == Role::kArcher ? Grade(forms.fortifyStamina, grade) : Grade(forms.fortifyHealth, grade));
			} else if (level >= 15 && r < 0.65f) {
				const int grade = Rules::PotionTier(level, 15, 15, 4);
				const int element = static_cast<int>(Roll(0.0f, 2.999f));
				extra = element == 0 ? Grade(forms.resistFire, grade) : (element == 1 ? Grade(forms.resistFrost, grade) : Grade(forms.resistShock, grade));
			}

			if (!main) {
				Log::Info("[Potions] {} finished brewing, but no potion was found in Skyrim.esm."sv, a_member.name);
				return;
			}
			a_actor->AddObjectToContainer(main, nullptr, count, nullptr);
			std::string line = std::format("{} brewed {}{}", a_member.name, count > 1 ? std::format("{} ", count) : ""s, main->GetName());
			if (extra) {
				a_actor->AddObjectToContainer(extra, nullptr, 1, nullptr);
				line += std::format(" and {}", extra->GetName());
			}
			line += ".";
			Notices::Push(line, Notices::Kind::kInfo);
			Log::Info("[Potions] {} (level {})"sv, line, level);
		}

		bool CasterOn(const Party::Member& a_member)
		{
			for (const auto& profile : Enemies::All()) {
				if (profile.caster && profile.target == a_member.handle) {
					return true;
				}
			}
			return false;
		}

		float restSince{ 0.0f };
	}

	Stock StockOf(RE::Actor* a_actor)
	{
		Stock stock;
		if (!a_actor) {
			return stock;
		}
		const auto items = a_actor->GetInventory([](RE::TESBoundObject& a_object) { return a_object.Is(RE::FormType::AlchemyItem); });
		for (const auto& [object, entry] : items) {
			const auto count = entry.first;
			if (count <= 0) {
				continue;
			}
			switch (KindOf(object ? object->As<RE::AlchemyItem>() : nullptr)) {
			case Kind::kHealing:
				stock.healing += count;
				break;
			case Kind::kMagicka:
				stock.magicka += count;
				break;
			case Kind::kStamina:
				stock.stamina += count;
				break;
			case Kind::kFortify:
			case Kind::kResist:
				stock.other += count;
				break;
			default:
				break;
			}
		}
		return stock;
	}

	void Fight(float a_dt)
	{
		const auto& settings = Settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		bool        hardFight = false;
		for (const auto& profile : Enemies::All()) {
			hardFight = hardFight || profile.boss || (player && profile.level >= player->GetLevel() + 5);
		}
		for (auto& member : Party::Members()) {
			member.drinkReady = std::max(member.drinkReady - a_dt, 0.0f);
			if (member.summon || !settings.drinkPotions || member.drinkReady > 0.0f) {
				continue;
			}
			auto        actor = member.handle.get();
			const auto* orders = Party::FindOrders(member.id);
			if (!actor || !orders || !orders->potions || !Game::Usable(actor.get()) || !actor->IsInCombat() || Abilities::Busy(actor.get())) {
				continue;
			}
			const float health = Game::Fraction(actor.get(), RE::ActorValue::kHealth);
			if (health < settings.drinkBelow && Drink(member, actor.get(), Strongest(actor.get(), Kind::kHealing), std::format("health {:.0f}%", health * 100.0f))) {
				continue;
			}
			const float magicka = Game::Fraction(actor.get(), RE::ActorValue::kMagicka);
			if (Caster(orders->role) && magicka < 0.2f &&
				Drink(member, actor.get(), Strongest(actor.get(), Kind::kMagicka), std::format("magicka {:.0f}%", magicka * 100.0f))) {
				continue;
			}
			const float stamina = Game::Fraction(actor.get(), RE::ActorValue::kStamina);
			if (!Caster(orders->role) && stamina < 0.15f &&
				Drink(member, actor.get(), Strongest(actor.get(), Kind::kStamina), std::format("stamina {:.0f}%", stamina * 100.0f))) {
				continue;
			}
			// Once a fight: a fortify against a boss, a resist against a caster on them.
			if (!member.fortified && hardFight) {
				member.fortified = true;
				if (Drink(member, actor.get(), Strongest(actor.get(), Kind::kFortify), "a hard fight")) {
					continue;
				}
			}
			if (!member.resisted && CasterOn(member)) {
				member.resisted = true;
				Drink(member, actor.get(), Strongest(actor.get(), Kind::kResist), "a caster on them");
			}
		}
	}

	void Rest(float a_dt)
	{
		restSince += a_dt;
		if (restSince < 0.5f) {
			return;
		}
		const float dt = restSince;
		restSince = 0.0f;

		const auto& settings = Settings::Get();
		auto*       player = RE::PlayerCharacter::GetSingleton();
		const bool  calm = player && !player->IsInCombat();
		for (auto& member : Party::Members()) {
			if (member.summon) {
				continue;
			}
			auto actor = member.handle.get();
			if (!actor || !Game::Usable(actor.get())) {
				member.brewing = -1.0f;
				continue;
			}
			if (member.brewExit >= 0.0f) {
				member.brewExit -= dt;
				if (member.brewExit < 0.0f && !member.brewAnimated) {
					actor->NotifyAnimationGraph("IdleForceDefaultState");
				}
			}
			const auto* orders = Party::FindOrders(member.id);
			const bool  allowed = settings.brewPotions && orders && orders->potions;
			if (!actor->IsInCombat()) {
				member.fortified = false;
				member.resisted = false;
			}

			// Standing about out of any fight: not walking, not in a chair or a bed (an alchemy
			// table is fine).
			const auto at = actor->GetPosition();
			const bool moved = at.GetDistance(member.lastSeen) > 30.0f;
			member.lastSeen = at;
			const bool seated = actor->AsActorState()->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal;
			const bool resting = calm && !actor->IsInCombat() && !moved && (!seated || AtStation(actor.get()));
			member.stillFor = resting ? member.stillFor + dt : 0.0f;

			if (member.brewing >= 0.0f) {
				if (!resting || !allowed) {
					StopBrew(member, actor.get(), !allowed ? "switched off"sv : (calm ? "they moved"sv : "a fight started"sv));
					continue;
				}
				member.brewing += dt;
				if (member.brewing >= member.brewTakes) {
					FinishBrew(member, actor.get(), *orders);
					member.brewIn = Roll(120.0f, 300.0f);
				}
				continue;
			}
			if (!allowed || !resting) {
				continue;
			}
			if (member.brewIn < 0.0f) {
				member.brewIn = Roll(60.0f, 180.0f);
			}
			member.brewIn -= dt;
			if (member.brewIn > 0.0f || member.stillFor < 6.0f) {
				continue;
			}
			// Not every quiet moment, and only when short of something.
			member.brewIn = Roll(90.0f, 240.0f);
			if (Need(actor.get(), *orders) == Kind::kNone || Roll(0.0f, 1.0f) > 0.6f) {
				continue;
			}
			StartBrew(member, actor.get());
		}
	}
}

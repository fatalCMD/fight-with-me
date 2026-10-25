#include "FWM/Leveling.h"

#include "FWM/Effects.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/PathsLink.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"

#ifdef PlaySound
#	undef PlaySound  // the Windows macro, not the game's function
#endif

namespace FWM::Leveling
{
	namespace
	{
		using Flag = RE::ACTOR_BASE_DATA::Flag;

		// Health, magicka and stamina, then the skills, in Skill's order.
		constexpr std::size_t kGrown = 3 + static_cast<std::size_t>(Skill::kCount);
		constexpr std::size_t kEntrySize = 68;

		struct Record
		{
			RE::FormID                npc{ 0 };
			std::uint16_t             level{ 1 };
			float                     xp{ 0.0f };
			std::uint16_t             levelField{ 0 };  // their plugin's: a level, or the share of yours in thousandths
			bool                      playerMult{ false };  // their plugin had them follow your level
			std::array<float, kGrown> grown{};
			bool                      applied{ true };  // what levels gave is on them
			bool                      held{ false };    // their record has had our level at least once
			std::uint64_t             levelUpAt{ 0 };
		};
		std::unordered_map<RE::FormID, Record> records;  // by the companion's reference

		struct Fallen
		{
			RE::ActorHandle who;
			RE::ActorHandle killer;
		};
		std::mutex              fallenLock;
		std::vector<Fallen>     fallen;
		std::vector<RE::FormID> fought;   // companions who have been in this fight
		std::vector<Foe>        tally;    // the enemies the party has brought down in it
		std::vector<RE::FormID> counted;  // and who they were: a death is reported as it starts and again once done
		std::uint64_t           payAt{ 0 };  // the fight is over: when to pay it out

		constexpr std::uint64_t kPayAfterMs = 2000;

		// Who has just levelled up, since when, and what their diamonds hold.
		struct Raised
		{
			RE::ActorHandle actor;
			std::uint64_t   at{ 0 };
			int             level{ 1 };
			int             points{ -1 };
		};
		std::vector<Raised> raised;
		std::mutex          badgeLock;
		std::vector<Badge>  badges;  // placed on screen each frame for the render thread

		Tuning TuningNow()
		{
			const auto& settings = Settings::Get();
			Tuning      tuning;
			tuning.xpPerEnemyLevel = settings.xpPerEnemyLevel;
			tuning.bossMult = settings.xpBossMult;
			tuning.climb = settings.xpClimb;
			tuning.crowd = settings.xpCrowd;
			// A level starts from what one of the player's would cost, whatever a mod has made that.
			if (auto* game = RE::GameSettingCollection::GetSingleton()) {
				if (auto* base = game->GetSetting("fXPLevelUpBase")) {
					tuning.costBase = base->GetFloat();
				}
				if (auto* mult = game->GetSetting("fXPLevelUpMult")) {
					tuning.costPerLevel = mult->GetFloat();
				}
			}
			return tuning;
		}

		RE::ActorValue ValueAt(std::size_t a_index)
		{
			switch (a_index) {
			case 0: return RE::ActorValue::kHealth;
			case 1: return RE::ActorValue::kMagicka;
			case 2: return RE::ActorValue::kStamina;
			default: break;
			}
			switch (static_cast<Skill>(a_index - 3)) {
			case Skill::kOneHanded: return RE::ActorValue::kOneHanded;
			case Skill::kTwoHanded: return RE::ActorValue::kTwoHanded;
			case Skill::kArchery: return RE::ActorValue::kArchery;
			case Skill::kBlock: return RE::ActorValue::kBlock;
			case Skill::kHeavyArmor: return RE::ActorValue::kHeavyArmor;
			case Skill::kLightArmor: return RE::ActorValue::kLightArmor;
			case Skill::kDestruction: return RE::ActorValue::kDestruction;
			case Skill::kRestoration: return RE::ActorValue::kRestoration;
			default: return RE::ActorValue::kAlteration;
			}
		}

		// What levels gave, put on or taken off again.
		void Lay(RE::Actor* a_actor, Record& a_record, bool a_on)
		{
			if (!a_actor || a_record.applied == a_on) {
				return;
			}
			auto* values = a_actor->AsActorValueOwner();
			for (std::size_t i = 0; i < kGrown; ++i) {
				if (a_record.grown[i] != 0.0f) {
					values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, ValueAt(i), a_on ? a_record.grown[i] : -a_record.grown[i]);
				}
			}
			a_record.applied = a_on;
		}

		// A level from a template is the template's to give; those are left alone.
		bool Templated(const RE::TESNPC* a_npc)
		{
			return a_npc->UsesTemplate() && a_npc->actorData.templateUseFlags.any(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kStats);
		}

		// Their record holds our level: no longer a share of the player's, no maximum. False when it
		// had to be put back.
		bool Hold(RE::TESNPC* a_npc, const Record& a_record)
		{
			auto& data = a_npc->actorData;
			if (!data.actorBaseFlags.all(Flag::kPCLevelMult) && data.level == a_record.level) {
				return true;
			}
			data.actorBaseFlags.reset(Flag::kPCLevelMult);
			data.level = a_record.level;
			return false;
		}

		void GiveBack(RE::TESNPC* a_npc, const Record& a_record)
		{
			auto& data = a_npc->actorData;
			data.level = a_record.levelField;
			if (a_record.playerMult) {
				data.actorBaseFlags.set(Flag::kPCLevelMult);
			} else {
				data.actorBaseFlags.reset(Flag::kPCLevelMult);
			}
		}

		void Grow(RE::Actor* a_actor, Record& a_record, const Growth& a_growth)
		{
			auto*                      values = a_actor->AsActorValueOwner();
			const std::array<float, 3> vitals{ a_growth.health, a_growth.magicka, a_growth.stamina };
			for (std::size_t i = 0; i < vitals.size(); ++i) {
				if (vitals[i] > 0.0f) {
					values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, ValueAt(i), vitals[i]);
					a_record.grown[i] += vitals[i];
				}
			}
			for (std::size_t s = 0; s < a_growth.skillCount; ++s) {
				const auto index = 3 + static_cast<std::size_t>(a_growth.skills[s]);
				if (values->GetActorValue(ValueAt(index)) < static_cast<float>(kSkillCap)) {
					values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, ValueAt(index), 1.0f);
					a_record.grown[index] += 1.0f;
				}
			}
		}

		// The highest level anyone may reach: the player's, unless that limit is off.
		int Cap()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return Settings::Get().levelCap && player ? std::max<int>(player->GetLevel(), 1) : kNoCap;
		}

		// Their new level, and the perk points they now have to spend, read once their record has
		// that level (the add-on counts points from it).
		void Raise(RE::FormID a_id, RE::Actor* a_actor, const Record& a_record)
		{
			Raised one{ a_actor->GetHandle(), ::GetTickCount64(), a_record.level, -1 };
			if (const auto* progress = PathsLink::ReadNow(a_id); progress && !progress->unlimited) {
				one.points = progress->Unspent();
			}
			std::erase_if(raised, [&](const Raised& a_old) { return a_old.actor == one.actor; });
			raised.push_back(one);
		}

		// Over their head, a little higher than the plan's marks, and only while they are seen.
		void PlaceBadges()
		{
			if (raised.empty()) {
				const std::scoped_lock guard(badgeLock);
				badges.clear();
				return;
			}
			const auto now = ::GetTickCount64();
			std::erase_if(raised, [now](const Raised& a_one) { return static_cast<float>(now - a_one.at) / 1000.0f >= kBadgeSeconds; });
			std::vector<Badge> placed;
			for (const auto& one : raised) {
				auto actor = one.actor.get();
				if (!actor || !actor->Is3DLoaded() || actor->IsDead()) {
					continue;
				}
				if (const auto at = Game::Project(Game::HeadPosition(actor.get()) + RE::NiPoint3{ 0.0f, 0.0f, 34.0f })) {
					placed.push_back({ at->first, at->second, static_cast<float>(now - one.at) / 1000.0f, one.level, one.points });
				}
			}
			const std::scoped_lock guard(badgeLock);
			badges.swap(placed);
		}

		Gain Earned(RE::FormID a_id, std::string_view a_name, RE::Actor* a_actor, Record& a_record, float a_xp, int a_cap, const Tuning& a_tuning)
		{
			const auto gain = Earn(a_record.level, a_record.xp, a_xp, a_cap, a_tuning);
			a_record.xp = gain.xp;
			if (gain.levels <= 0) {
				return gain;
			}
			Lay(a_actor, a_record, true);
			const auto* orders = Party::FindOrders(a_id);
			const auto  growth = GrowthFor(orders ? orders->role : Role::kFighter);
			for (int i = 0; i < gain.levels; ++i) {
				Grow(a_actor, a_record, growth);
			}
			a_record.level = static_cast<std::uint16_t>(std::min(static_cast<int>(a_record.level) + gain.levels, 65535));
			a_record.levelUpAt = ::GetTickCount64();
			if (auto* npc = a_actor->GetActorBase()) {
				(void)Hold(npc, a_record);
				a_record.held = true;
			}

			Notices::Push(Trf("{} reaches level {}.", a_name, a_record.level), Notices::Kind::kOrder);
			RE::PlaySound("UILevelUpSD");
			Effects::Flash(a_actor, Effects::Look::kLevelUp, 3.2f);  // only with ability effects on
			Raise(a_id, a_actor, a_record);
			Log::Info("[Level] {} reaches level {}{} (+{:.0f} health, +{:.0f} magicka, +{:.0f} stamina a level as a {})."sv, a_name, a_record.level,
				gain.capped ? ", your level: no more XP until you level up" : "", growth.health, growth.magicka, growth.stamina,
				orders ? Info(orders->role).name : "Fighter");
			return gain;
		}

		// The fight is over: each companion who was in it earns its enemies' worth at once.
		void Pay()
		{
			payAt = 0;
			if (!tally.empty() && Settings::Get().leveling) {
				const auto  tuning = TuningNow();
				const int   cap = Cap();
				std::string said;
				for (const auto id : fought) {
					auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
					auto  it = records.find(id);
					if (!actor || actor->IsDead() || it == records.end()) {
						continue;
					}
					auto&       record = it->second;
					const char* name = actor->GetDisplayFullName();
					const int   was = record.level;
					const float xp = was >= cap ? 0.0f : FightXP(tally, was, tuning);
					const auto  gain = Earned(id, name ? name : "?", actor, record, xp, cap, tuning);
					said += std::format("{}{} ", said.empty() ? "" : "; ", name ? name : "?");
					if (was >= cap) {
						said += std::format("nothing (level {}, at or above yours)", was);
					} else {
						said += std::format("+{:.0f} XP{} (level {}, {:.0f} of {:.0f}{})", xp, gain.levels ? std::format(", {} level(s) up", gain.levels) : std::string(),
							record.level, record.xp, Cost(record.level, tuning), gain.capped ? ", now at your level" : "");
					}
				}
				int bosses = 0;
				for (const auto& foe : tally) {
					bosses += foe.boss ? 1 : 0;
				}
				Log::Info("[Level] The fight is over: {} enem{} down{}. {}."sv, tally.size(), tally.size() == 1 ? "y" : "ies",
					bosses ? std::format(", {} a boss", bosses) : std::string(), said.empty() ? "Nobody who fought is here to earn it"s : said);
			}
			tally.clear();
			counted.clear();
			fought.clear();
		}

		struct DeathSink final : RE::BSTEventSink<RE::TESDeathEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				if (!a_event || !a_event->actorDying) {  // dying or dead: Update counts each enemy once
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* who = a_event->actorDying->As<RE::Actor>();
				if (!who) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto*                  killer = a_event->actorKiller ? a_event->actorKiller->As<RE::Actor>() : nullptr;
				const std::scoped_lock guard(fallenLock);
				fallen.push_back({ who->GetHandle(), killer ? killer->GetHandle() : RE::ActorHandle{} });
				return RE::BSEventNotifyControl::kContinue;
			}
		} deathSink;

		void Write(std::vector<std::uint8_t>& a_out, const void* a_data, std::size_t a_size)
		{
			const auto at = a_out.size();
			a_out.resize(at + a_size);
			std::memcpy(a_out.data() + at, a_data, a_size);
		}

		template <class T>
		T Read(const std::uint8_t*& a_at)
		{
			T value{};
			std::memcpy(&value, a_at, sizeof(T));
			a_at += sizeof(T);
			return value;
		}
	}

	void Refresh()
	{
		if (!Settings::Get().leveling) {
			// Off: everyone back to their own plugin's level, without what levels gave them.
			for (auto& [id, record] : records) {
				if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(record.npc)) {
					GiveBack(npc, record);
				}
				Lay(RE::TESForm::LookupByID<RE::Actor>(id), record, false);
				record.held = false;
			}
			return;
		}
		for (const auto& member : Party::Members()) {
			if (member.summon) {
				continue;
			}
			auto  actor = member.handle.get();
			auto* npc = actor ? actor->GetActorBase() : nullptr;
			if (!npc || Templated(npc)) {
				continue;
			}
			auto it = records.find(member.id);
			if (it == records.end()) {
				Record record;
				record.npc = npc->GetFormID();
				record.level = static_cast<std::uint16_t>(std::clamp<int>(actor->GetLevel(), 1, 65535));
				record.levelField = npc->actorData.level;
				record.playerMult = npc->actorData.actorBaseFlags.all(Flag::kPCLevelMult);
				it = records.emplace(member.id, record).first;
				Log::Info("[Level] {} is taken up at level {}{}."sv, member.name, record.level,
					record.playerMult ? " (their own mod had them follow your level; from here on they level by fighting)" : "");
			}
			auto& record = it->second;
			if (!Hold(npc, record) && record.held) {
				Log::Info("[Level] Something changed {}'s level (another mod's \"level up\"?); Fight With Me keeps them at level {}."sv, member.name, record.level);
			}
			record.held = true;
			Lay(actor.get(), record, true);
		}
	}

	void Update(bool a_fighting)
	{
		PlaceBadges();
		if (a_fighting) {
			payAt = 0;  // back in it before the pay-out: the same fight goes on
			for (const auto& member : Party::Members()) {
				auto actor = member.summon ? nullptr : member.handle.get();
				if (actor && actor->IsInCombat() && std::find(fought.begin(), fought.end(), member.id) == fought.end()) {
					fought.push_back(member.id);
				}
			}
		}
		std::vector<Fallen> now;
		{
			const std::scoped_lock guard(fallenLock);
			now.swap(fallen);
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!now.empty() && !fought.empty() && player && Settings::Get().leveling) {
			for (const auto& one : now) {
				auto enemy = one.who.get();
				if (!enemy || enemy.get() == player || enemy->IsPlayerTeammate() || Party::IsPartyActor(enemy.get())) {
					continue;
				}
				if (std::find(counted.begin(), counted.end(), enemy->GetFormID()) != counted.end()) {
					continue;
				}
				// An enemy of the party, not a bystander someone else cut down.
				auto       killer = one.killer.get();
				auto       target = Game::CombatTarget(enemy.get());
				const bool ours = killer && (killer.get() == player || Party::IsPartyActor(killer.get()));
				const bool atUs = target && (target.get() == player || Party::IsPartyActor(target.get()));
				if (!ours && !atUs && !enemy->IsHostileToActor(player)) {
					continue;
				}
				counted.push_back(enemy->GetFormID());
				tally.push_back({ static_cast<int>(enemy->GetLevel()), Game::IsBoss(enemy.get()) });
			}
		}
		if (payAt && ::GetTickCount64() >= payAt) {
			Pay();
		}
	}

	void FightOver()
	{
		if (!fought.empty()) {
			payAt = ::GetTickCount64() + kPayAfterMs;
		}
	}

	std::optional<Progress> Find(RE::FormID a_id)
	{
		const auto it = records.find(a_id);
		if (it == records.end() || !Settings::Get().leveling) {
			return std::nullopt;
		}
		const auto& record = it->second;
		Progress    progress;
		progress.level = record.level;
		progress.xp = record.xp;
		progress.next = Cost(record.level, TuningNow());
		progress.sinceLevelUp = record.levelUpAt ? static_cast<float>(::GetTickCount64() - record.levelUpAt) / 1000.0f : 99.0f;
		progress.capped = record.level >= Cap();
		return progress;
	}

	std::vector<Badge> Badges()
	{
		const std::scoped_lock guard(badgeLock);
		return badges;
	}

	void InstallEvents()
	{
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESDeathEvent>(&deathSink);
		}
	}

	std::vector<std::uint8_t> Encode()
	{
		std::vector<std::uint8_t> out;
		out.reserve(records.size() * kEntrySize);
		for (const auto& [id, record] : records) {
			const std::uint8_t  mult = record.playerMult ? 1 : 0;
			const std::uint8_t  applied = record.applied ? 1 : 0;
			const std::uint16_t pad = 0;
			Write(out, &id, 4);
			Write(out, &record.npc, 4);
			Write(out, &record.level, 2);
			Write(out, &record.levelField, 2);
			Write(out, &mult, 1);
			Write(out, &applied, 1);
			Write(out, &pad, 2);
			Write(out, &record.xp, 4);
			Write(out, record.grown.data(), kGrown * 4);
		}
		return out;
	}

	void Decode(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, const std::function<RE::FormID(RE::FormID)>& a_resolve)
	{
		if (a_version != kSaveVersion) {
			return;
		}
		int loaded = 0, dropped = 0;
		for (std::size_t at = 0; at + kEntrySize <= a_bytes.size(); at += kEntrySize) {
			const std::uint8_t* p = a_bytes.data() + at;
			const auto          id = a_resolve(Read<std::uint32_t>(p));
			Record              record;
			record.npc = a_resolve(Read<std::uint32_t>(p));
			record.level = std::max<std::uint16_t>(Read<std::uint16_t>(p), 1);
			record.levelField = Read<std::uint16_t>(p);
			record.playerMult = Read<std::uint8_t>(p) != 0;
			record.applied = Read<std::uint8_t>(p) != 0;
			(void)Read<std::uint16_t>(p);
			record.xp = std::max(Read<float>(p), 0.0f);
			for (auto& grown : record.grown) {
				grown = Read<float>(p);
			}
			auto* npc = record.npc ? RE::TESForm::LookupByID<RE::TESNPC>(record.npc) : nullptr;
			if (!id || !npc) {
				++dropped;  // the follower's plugin is gone
				continue;
			}
			if (Settings::Get().leveling) {
				(void)Hold(npc, record);
				record.held = true;
			}
			records[id] = record;
			++loaded;
		}
		Log::Info("[Level] Levels for {} companion(s) loaded{}."sv, loaded, dropped ? std::format(", {} from removed plugins dropped", dropped) : std::string());
	}

	void Reset()
	{
		for (const auto& [id, record] : records) {
			if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(record.npc)) {
				GiveBack(npc, record);
			}
		}
		records.clear();
		fought.clear();
		tally.clear();
		counted.clear();
		payAt = 0;
		raised.clear();
		{
			const std::scoped_lock badgeGuard(badgeLock);
			badges.clear();
		}
		const std::scoped_lock guard(fallenLock);
		fallen.clear();
	}
}

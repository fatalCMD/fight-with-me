#include "FWM/Traits.h"

#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Party.h"

namespace FWM::Traits
{
	namespace
	{
		using EntryPoint = RE::BGSEntryPoint::ENTRY_POINT;
		using Function = RE::FUNCTION_DATA::FunctionID;
		using OpCode = RE::CONDITION_ITEM_DATA::OpCode;
		using Archetype = RE::EffectArchetypes::ArchetypeID;

		constexpr float         kLookAgain = 60.0f;   // seconds out of a fight between looks at someone already known
		constexpr float         kBlink = 600.0f;      // farther than this in one frame is not walking
		constexpr std::uint64_t kSayEvery = 2000;     // ms between log lines about one trait

		struct Note
		{
			std::string         name;
			std::string         what;
			std::optional<Kind> kind;
			bool                hide{ false };
			std::string         when;
		};
		std::unordered_map<RE::FormID, Note>  notesByForm;
		std::unordered_map<std::string, Note> notesByScript;  // lower case

		struct Known
		{
			Profile       profile;
			std::uint64_t at{ 0 };
			std::string   said;  // what the log last said about them, to speak only of changes
		};
		std::unordered_map<RE::FormID, Known> known;

		std::mutex                                                          ownersLock;
		std::unordered_map<RE::FormID, std::pair<RE::FormID, std::string>> owners;  // magic effect -> (companion, trait)
		std::mutex                                                          saidLock;
		std::unordered_map<std::uint64_t, std::uint64_t>                    saidAt;

		struct Watch
		{
			RE::NiPoint3 at{};
			bool         placed{ false };
			bool         fleeing{ false };
		};
		std::unordered_map<RE::FormID, Watch> watches;

		// The effects of the spells behind traits, each with the trait's name.
		using Effects = std::vector<std::pair<RE::FormID, std::string>>;

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string_view Trimmed(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t");
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return first == std::string_view::npos ? std::string_view{} : a_text.substr(first, last - first + 1);
		}

		bool Says(std::uint64_t a_key)
		{
			const auto             now = ::GetTickCount64();
			const std::scoped_lock guard(saidLock);
			auto&                  at = saidAt[a_key];
			if (at && now - at < kSayEvery) {
				return false;
			}
			at = now;
			return true;
		}

		// The base game and Creation Club give no one a trait of their own.
		bool BaseGame(const RE::TESForm* a_form)
		{
			const auto* file = a_form ? a_form->GetFile(0) : nullptr;
			if (!file) {
				return true;
			}
			const auto name = Lower(file->GetFilename());
			return name == "skyrim.esm" || name == "update.esm" || name == "dawnguard.esm" || name == "hearthfires.esm" || name == "dragonborn.esm" ||
			       name.starts_with("cc");
		}

		std::optional<Kind> KindNamed(std::string_view a_text)
		{
			const auto text = Lower(Trimmed(a_text));
			for (std::size_t i = 0; i < static_cast<std::size_t>(Kind::kCount); ++i) {
				if (Lower(KindName(static_cast<Kind>(i))) == text) {
					return static_cast<Kind>(i);
				}
			}
			return std::nullopt;
		}

		// --- Reading conditions ------------------------------------------------------------

		struct Reading
		{
			Trigger trigger;
			bool    locked{ false };  // a global that keeps it back is not where it must be
			bool    fight{ false };   // asks for a fight, or for a state only a fight brings
		};

		float Compared(const RE::CONDITION_ITEM_DATA& a_data)
		{
			if (a_data.flags.global) {
				return a_data.comparisonValue.g ? a_data.comparisonValue.g->value : 0.0f;
			}
			return a_data.comparisonValue.f;
		}

		bool Holds(float a, OpCode a_op, float b)
		{
			switch (a_op) {
			case OpCode::kEqualTo: return a == b;
			case OpCode::kNotEqualTo: return a != b;
			case OpCode::kGreaterThan: return a > b;
			case OpCode::kGreaterThanOrEqualTo: return a >= b;
			case OpCode::kLessThan: return a < b;
			default: return a <= b;
			}
		}

		bool Below(OpCode a_op) { return a_op == OpCode::kLessThan || a_op == OpCode::kLessThanOrEqualTo; }
		bool Above(OpCode a_op) { return a_op == OpCode::kGreaterThan || a_op == OpCode::kGreaterThanOrEqualTo; }

		// What a condition list says about when: chance, health limits, power attacks, and globals
		// that keep it back for now. Conditions in an OR group say nothing alone and are skipped.
		void Read(const RE::TESCondition& a_conditions, Reading& a_out, bool a_whenToo = true)
		{
			bool inOr = false;
			for (auto* item = a_conditions.head; item; item = item->next) {
				const auto& d = item->data;
				const bool  loose = inOr || d.flags.isOR;
				inOr = d.flags.isOR;
				if (loose) {
					continue;
				}
				const auto  op = d.flags.opCode;
				const float value = Compared(d);
				const bool  onTarget = d.object.get() == RE::CONDITIONITEMOBJECT::kTarget || d.object.get() == RE::CONDITIONITEMOBJECT::kCombatTarget;
				switch (d.functionData.function.get()) {
				case Function::kGetGlobalValue:
					if (auto* global = static_cast<RE::TESGlobal*>(d.functionData.params[0]); global && !Holds(global->value, op, value)) {
						a_out.locked = true;
					}
					break;
				case Function::kIsInCombat:
				case Function::kGetCombatState:
				case Function::kIsAttacking:
				case Function::kIsBleedingOut:
					// "Not in combat" is a utility/sandbox condition, not a combat trigger.
					a_out.fight |= Holds(1.0f, op, value) && !Holds(0.0f, op, value);
					break;
				default:
					break;
				}
				if (!a_whenToo) {
					continue;
				}
				switch (d.functionData.function.get()) {
				case Function::kGetRandomPercent:
					if (Below(op)) {
						a_out.trigger.chance = std::clamp(static_cast<int>(value), 0, 100);
					}
					break;
				case Function::kIsPowerAttacking:
					if ((op == OpCode::kEqualTo && value >= 1.0f) || (op == OpCode::kNotEqualTo && value == 0.0f)) {
						a_out.trigger.moment = Moment::kPowerAttack;
						a_out.fight = true;
					}
					break;
				case Function::kGetActorValuePercent:
				case Function::kGetHealthPercentage:
					{
						const bool health = d.functionData.function.get() == Function::kGetHealthPercentage ||
						                    static_cast<RE::ActorValue>(reinterpret_cast<std::uintptr_t>(d.functionData.params[0])) == RE::ActorValue::kHealth;
						const float fraction = value > 1.0f ? value / 100.0f : value;
						if (health && onTarget && Above(op)) {
							a_out.trigger.targetHealthAbove = fraction;
						} else if (health && !onTarget && Below(op)) {
							a_out.trigger.healthBelow = fraction;
							a_out.fight = true;
						}
					}
					break;
				default:
					break;
				}
			}
		}

		// What a spell does to the one it lands on, by its effects.
		Kind KindOf(const RE::SpellItem* a_spell, Moment a_moment)
		{
			bool evade = false, summon = false, aura = false;
			for (const auto* effect : a_spell->effects) {
				const auto* base = effect ? effect->baseEffect : nullptr;
				if (!base) {
					continue;
				}
				switch (base->GetArchetype()) {
				case Archetype::kInvisibility:
				case Archetype::kEtherealize:
					evade = true;
					break;
				case Archetype::kSummonCreature:
				case Archetype::kReanimate:
					summon = true;
					break;
				case Archetype::kCloak:
					aura = true;
					break;
				default:
					break;
				}
			}
			if (summon) {
				return Kind::kSummon;
			}
			if (evade) {
				return Kind::kEvade;
			}
			if (aura) {
				return Kind::kAura;
			}
			return a_moment == Moment::kHit ? Kind::kOnHit : Kind::kStrike;
		}

		// The spell's own effects can be kept back too (a quest not done yet).
		bool SpellLocked(const RE::SpellItem* a_spell)
		{
			Reading reading;
			for (const auto* effect : a_spell->effects) {
				if (effect) {
					Read(effect->conditions, reading, false);
					if (effect->baseEffect) {
						Read(effect->baseEffect->conditions, reading, false);
					}
				}
			}
			return reading.locked;
		}

		const Note* NoteFor(std::initializer_list<const RE::TESForm*> a_forms)
		{
			for (const auto* form : a_forms) {
				if (form) {
					if (const auto it = notesByForm.find(form->GetFormID()); it != notesByForm.end()) {
						return &it->second;
					}
				}
			}
			return nullptr;
		}

		bool Named(const Note* a_note)
		{
			return a_note && !a_note->hide && !a_note->name.empty();
		}

		bool TheirRecord(const RE::TESForm* a_form, const RE::TESNPC* a_npc, bool a_noted = false)
		{
			const auto* source = a_form ? a_form->GetFile(0) : nullptr;
			const auto* owner = a_npc ? a_npc->GetFile(0) : nullptr;
			return SignatureSource(source ? source->GetFilename() : "", owner ? owner->GetFilename() : "", a_noted);
		}

		// A script effect may only start dialogue, manage a package or paint blood on a
		// character. Conditions alone cannot establish a combat ability: those effects
		// need a named note describing what they do.
		bool HasCombatEffect(const RE::SpellItem* a_spell)
		{
			return std::any_of(a_spell->effects.begin(), a_spell->effects.end(), [](const RE::Effect* effect) {
				if (!effect || !effect->baseEffect) {
					return false;
				}
				switch (effect->baseEffect->GetArchetype()) {
				case Archetype::kNone:
				case Archetype::kScript:
				case Archetype::kLight:
				case Archetype::kDarkness:
				case Archetype::kNightEye:
				case Archetype::kDetectLife:
				case Archetype::kGuide:
					return false;
				default:
					return true;
				}
			});
		}

		// A trait from a spell, named and worded, notes first. False when a note hides it.
		bool Add(std::vector<Trait>& a_out, Effects& a_effects, const RE::SpellItem* a_spell, const RE::BGSPerk* a_perk,
			const Reading& a_reading, std::string_view a_owner)
		{
			const auto* note = NoteFor({ a_spell, a_perk });
			if (note && note->hide) {
				return false;
			}
			Trait trait;
			trait.name = note && !note->name.empty() ? note->name : PickName(a_spell->GetName(), a_perk ? a_perk->GetName() : "", a_owner);
			if (trait.name.empty()) {
				trait.name = Tr("Signature move");
			}
			trait.when = note && !note->when.empty() ? note->when : Describe(a_reading.trigger);
			trait.what = note ? note->what : std::string{};
			trait.kind = note && note->kind ? *note->kind : KindOf(a_spell, a_reading.trigger.moment);
			trait.locked = a_reading.locked || SpellLocked(a_spell);
			trait.key = a_perk ? a_perk->GetFormID() : a_spell->GetFormID();
			for (const auto* effect : a_spell->effects) {
				if (effect && effect->baseEffect) {
					a_effects.emplace_back(effect->baseEffect->GetFormID(), trait.name);
				}
			}
			a_out.push_back(std::move(trait));
			return true;
		}

		// An ability needs both a combat trigger and a combat effect (or a named note).
		void FromAbility(std::vector<Trait>& a_out, Effects& a_effects, const RE::SpellItem* a_spell, const RE::BGSPerk* a_perk,
			RE::TESNPC* a_npc, std::string_view a_owner)
		{
			if (!a_spell || a_spell->GetSpellType() != RE::MagicSystem::SpellType::kAbility) {
				return;
			}
			const bool noted = Named(NoteFor({ a_spell, a_perk }));
			if ((!TheirRecord(a_spell, a_npc, noted) && !TheirRecord(a_perk, a_npc)) || (!noted && !HasCombatEffect(a_spell))) {
				return;
			}
			Reading reading;
			reading.trigger.moment = Moment::kInFight;
			for (const auto* effect : a_spell->effects) {
				if (effect) {
					Read(effect->conditions, reading);
					if (effect->baseEffect) {
						Read(effect->baseEffect->conditions, reading);
					}
				}
			}
			if (a_perk) {
				Read(a_perk->perkConditions, reading);
			}
			if (reading.fight) {
				Add(a_out, a_effects, a_spell, a_perk, reading, a_owner);
			}
		}

		void FromPerks(std::vector<Trait>& a_out, Effects& a_effects, RE::TESNPC* a_npc, std::string_view a_owner)
		{
			for (std::uint32_t i = 0; i < a_npc->perkCount; ++i) {
				const auto* perk = a_npc->perks ? a_npc->perks[i].perk : nullptr;
				if (!perk) {
					continue;
				}
				for (auto* entry : perk->perkEntries) {
					if (!entry) {
						continue;
					}
					if (entry->GetType() == RE::PERK_ENTRY_TYPE::kAbility) {
						FromAbility(a_out, a_effects, static_cast<RE::BGSAbilityPerkEntry*>(entry)->ability, perk, a_npc, a_owner);
						continue;
					}
					if (entry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
						continue;
					}
					auto*  point = static_cast<RE::BGSEntryPointPerkEntry*>(entry);
					Moment moment = Moment::kSwing;
					switch (point->entryData.entryPoint.get()) {
					case EntryPoint::kApplyWeaponSwingSpell: moment = Moment::kSwing; break;
					case EntryPoint::kApplyCombatHitSpell: moment = Moment::kHit; break;
					case EntryPoint::kApplyBashingSpell: moment = Moment::kBash; break;
					default: continue;
					}
					auto* data = point->functionData;
					if (!data || data->GetType() != RE::BGSEntryPointFunctionData::ENTRY_POINT_FUNCTION_DATA::kSpellItem) {
						continue;
					}
					const auto* spell = static_cast<RE::BGSEntryPointFunctionDataSpellItem*>(data)->spell;
					if (!spell) {
						continue;
					}
					const bool noted = Named(NoteFor({ spell, perk }));
					if ((!TheirRecord(perk, a_npc, noted) && !TheirRecord(spell, a_npc)) || (!noted && !HasCombatEffect(spell))) {
						continue;
					}
					Reading reading;
					reading.trigger.moment = moment;
					Read(perk->perkConditions, reading);
					for (std::uint32_t tab = 0; tab < point->conditions.size(); ++tab) {
						Read(point->conditions[tab], reading);
					}
					Add(a_out, a_effects, spell, perk, reading, a_owner);
				}
			}
		}

		void FromSpellList(std::vector<Trait>& a_out, Effects& a_effects, RE::TESNPC* a_npc, std::string_view a_owner)
		{
			const auto* list = a_npc->GetSpellList();
			for (std::uint32_t i = 0; list && list->spells && i < list->numSpells; ++i) {
				FromAbility(a_out, a_effects, list->spells[i], nullptr, a_npc, a_owner);
			}
		}

		// Their own mod's quests: a script on the alias that holds them, answering a fight starting,
		// a blow, or going down. Follower frameworks' scripts belong to another plugin and are left out.
		void FromScripts(std::vector<Trait>& a_out, RE::Actor* a_actor, RE::TESNPC* a_npc)
		{
			const auto* home = a_npc->GetFile(0);
			auto*       aliases = a_actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
			auto*       vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!home || !aliases || !vm) {
				return;
			}
			auto* policy = vm->GetObjectHandlePolicy();
			if (!policy) {
				return;
			}
			std::vector<std::pair<const RE::TESQuest*, const RE::BGSBaseAlias*>> theirs;
			{
				RE::BSReadLockGuard lock(aliases->lock);
				for (const auto* data : aliases->aliases) {
					if (data && data->quest && data->alias && data->quest->GetFile(0) == home) {
						theirs.emplace_back(data->quest, data->alias);
					}
				}
			}

			struct Found
			{
				std::string           script;
				std::vector<Moment>   moments;
				std::vector<std::string> extra;  // "when they are hit", "when they go down"
				RE::FormID            quest{ 0 };
			};
			std::vector<Found> found;
			for (const auto& [quest, alias] : theirs) {
				const auto handle = policy->GetHandleForObject(RE::BGSRefAlias::VMTYPEID, alias);
				RE::BSSpinLockGuard lock(vm->attachedScriptsLock);
				const auto it = vm->attachedScripts.find(handle);
				if (it == vm->attachedScripts.end()) {
					continue;
				}
				for (std::uint32_t i = 0; i < it->second.size(); ++i) {
					const auto* object = it->second[i].get();
					const auto* type = object ? object->GetTypeInfo() : nullptr;
					if (!type) {
						continue;
					}
					Found one{ type->GetName(), {}, {}, quest->GetFormID() };
					// Handlers can sit on a parent script too, up to the game's own types.
					for (auto* t = type; t; t = t->GetParent()) {
						const std::string_view name = t->GetName();
						if (name == "ReferenceAlias" || name == "Alias") {
							break;
						}
						const auto* funcs = t->GetMemberFuncIter();
						for (std::uint32_t f = 0; funcs && f < t->GetNumMemberFuncs(); ++f) {
							const auto& func = funcs[f].func;
							if (!func) {
								continue;
							}
							const std::string_view event = func->GetName().c_str();
							if (event == "OnCombatStateChanged") {
								one.moments.push_back(Moment::kFightStart);
							} else if (event == "OnHit") {
								one.extra.push_back(std::string(Tr("when they are hit")));
							} else if (event == "OnEnterBleedout") {
								one.extra.push_back(std::string(Tr("when they go down")));
							}
						}
					}
					if (!one.moments.empty() || !one.extra.empty()) {
						found.push_back(std::move(one));
					}
				}
			}

			for (auto& one : found) {
				const auto note = notesByScript.find(Lower(one.script));
				// An event handler is not evidence of an ability: many only play dialogue.
				if (note == notesByScript.end() || !Named(&note->second)) {
					continue;
				}
				Trait trait;
				trait.name = note->second.name;
				std::string when = one.moments.empty() ? std::string{} : Describe({ Moment::kFightStart });
				for (auto& extra : one.extra) {
					if (when.find(extra) != std::string::npos) {
						continue;
					}
					if (when.empty()) {
						extra[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(extra[0])));
						when = extra;
					} else {
						when += std::format("{}{}", Tr(", or "), extra);
					}
				}
				trait.when = note->second.when.empty() ? std::move(when) : note->second.when;
				trait.what = note->second.what;
				trait.kind = note->second.kind.value_or(Kind::kInstinct);
				trait.key = one.quest;
				a_out.push_back(std::move(trait));
			}
		}

		Profile Detect(RE::Actor* a_actor, Effects& a_effects)
		{
			Profile profile;
			auto*   npc = a_actor->GetActorBase();
			if (!npc) {
				return profile;
			}
			const auto owner = Game::Name(a_actor);
			if (!BaseGame(npc)) {
				FromPerks(profile.traits, a_effects, npc, owner);
				FromSpellList(profile.traits, a_effects, npc, owner);
				FromScripts(profile.traits, a_actor, npc);
			}
			profile.confidence = static_cast<int>(a_actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kConfidence));
			if (LosesNerve(profile.confidence)) {
				profile.traits.push_back({ std::string(Tr("Loses nerve")), NerveText(profile.confidence), std::string{}, Kind::kNerve });
			}
			Merge(profile.traits);
			profile.rarity = RarityFor(Counted(profile.traits));
			return profile;
		}

		std::string Summary(const Profile& a_profile)
		{
			std::string out = std::format("{} ({})", RarityName(a_profile.rarity), Counted(a_profile.traits));
			for (const auto& trait : a_profile.traits) {
				out += std::format("; {} [{}{}]: {}", trait.name, KindName(trait.kind), trait.locked ? ", locked for now" : "", trait.when);
			}
			return out;
		}

		struct ApplySink final : RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
			{
				if (!a_event || !a_event->caster) {
					return RE::BSEventNotifyControl::kContinue;
				}
				std::string who, trait;
				{
					const std::scoped_lock guard(ownersLock);
					const auto            it = owners.find(a_event->magicEffect);
					if (it == owners.end() || it->second.first != a_event->caster->GetFormID()) {
						return RE::BSEventNotifyControl::kContinue;
					}
					if (!Says((static_cast<std::uint64_t>(it->second.first) << 32) ^ std::hash<std::string>{}(it->second.second))) {
						return RE::BSEventNotifyControl::kContinue;
					}
					who = a_event->caster->GetDisplayFullName();
					trait = it->second.second;
				}
				auto*      target = a_event->target.get();
				const bool self = target == a_event->caster.get();
				Log::Info("[Traits] {} uses {}{}."sv, who, trait, self || !target ? std::string() : std::format(" on {}", target->GetDisplayFullName()));
				return RE::BSEventNotifyControl::kContinue;
			}
		} applySink;
	}

	void LoadNotes()
	{
		notesByForm.clear();
		notesByScript.clear();
		std::error_code ec;
		const auto      folder = std::filesystem::current_path(ec) / L"Data" / L"SKSE" / L"Plugins" / L"FightWithMe" / L"Traits";
		if (!std::filesystem::is_directory(folder, ec)) {
			return;
		}
		auto* data = RE::TESDataHandler::GetSingleton();
		int   files = 0, entries = 0, unknown = 0;
		for (const auto& file : std::filesystem::directory_iterator(folder, ec)) {
			if (!file.is_regular_file(ec) || Lower(file.path().extension().string()) != ".ini") {
				continue;
			}
			++files;
			const auto      path = file.path().wstring();
			std::vector<wchar_t> sections(32768);
			const auto      length = ::GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), path.c_str());
			for (const wchar_t* section = sections.data(); section < sections.data() + length && *section; section += std::wcslen(section) + 1) {
				auto read = [&](const wchar_t* a_key) {
					std::vector<wchar_t> value(1024);
					::GetPrivateProfileStringW(section, a_key, L"", value.data(), static_cast<DWORD>(value.size()), path.c_str());
					return SKSE::stl::utf16_to_utf8(value.data()).value_or(std::string{});
				};
				Note note{ read(L"Name"), read(L"What"), KindNamed(read(L"Kind")), read(L"Hide") == "1", read(L"When") };
				const auto key = SKSE::stl::utf16_to_utf8(section).value_or(std::string{});
				if (Lower(key).starts_with("script:")) {
					notesByScript[Lower(Trimmed(std::string_view(key).substr(7)))] = std::move(note);
					++entries;
					continue;
				}
				// "Plugin.esp|0x123456": the id as the plugin has it, its load order taken off.
				const auto bar = key.find('|');
				if (bar == std::string::npos || !data) {
					++unknown;
					continue;
				}
				const auto   plugin = std::string(Trimmed(std::string_view(key).substr(0, bar)));
				std::uint32_t local = 0;
				try {
					local = static_cast<std::uint32_t>(std::stoul(std::string(Trimmed(std::string_view(key).substr(bar + 1))), nullptr, 16));
				} catch (...) {
					++unknown;
					continue;
				}
				const auto id = data->LookupFormID(local & 0xFFFFFF, plugin);
				if (!id) {
					++unknown;  // that plugin is not loaded, which is fine
					continue;
				}
				notesByForm[id] = std::move(note);
				++entries;
			}
		}
		Log::Info("[Traits] Notes: {} entries from {} file(s){}."sv, entries, files, unknown ? std::format(" ({} for plugins not loaded)", unknown) : std::string());
	}

	void Refresh()
	{
		const auto now = ::GetTickCount64();
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const bool fighting = player && player->IsInCombat();
		for (const auto& member : Party::Members()) {
			if (member.summon) {
				continue;
			}
			auto actor = member.handle.get();
			if (!actor) {
				continue;
			}
			auto& entry = known[member.id];
			if (entry.at && (fighting || actor->IsInCombat() || now - entry.at < static_cast<std::uint64_t>(kLookAgain * 1000.0f))) {
				continue;
			}
			Effects effects;
			entry.profile = Detect(actor.get(), effects);
			entry.at = now;
			{
				// Every effect of a trait's spell points back at the trait, for the log.
				const std::scoped_lock guard(ownersLock);
				std::erase_if(owners, [&](const auto& a_pair) { return a_pair.second.first == member.id; });
				for (auto& [effect, trait] : effects) {
					owners[effect] = { member.id, std::move(trait) };
				}
			}
			const auto summary = Summary(entry.profile);
			if (summary != entry.said) {
				entry.said = summary;
				Log::Info("[Traits] {}: {}."sv, member.name, summary);
			}
		}
	}

	const Profile* Find(RE::FormID a_id)
	{
		const auto it = known.find(a_id);
		return it == known.end() || !it->second.at ? nullptr : &it->second.profile;
	}

	void Tick(float)
	{
		for (const auto& member : Party::Members()) {
			if (member.summon) {
				continue;
			}
			auto actor = member.handle.get();
			if (!actor) {
				continue;
			}
			auto& watch = watches[member.id];
			if (!actor->IsInCombat()) {
				watch = {};
				continue;
			}
			const auto at = actor->GetPosition();
			if (watch.placed && at.GetDistance(watch.at) > kBlink && Says(0xB11Cull ^ member.id)) {
				auto        target = Game::CombatTarget(actor.get());
				auto        planned = member.desired.get();
				std::string where = target ? std::format("; now {:.0f} from {}", Game::Distance(actor.get(), target.get()), Game::Name(target.get())) : std::string();
				if (target && planned) {
					where += planned.get() == target.get() ? ", the plan's pick" : std::format(" (the plan wanted {})", Game::Name(planned.get()));
				}
				Log::Info("[Traits] {} blinks {:.0f} across the field{}."sv, member.name, at.GetDistance(watch.at), where);
			}
			watch.at = at;
			watch.placed = true;

			const auto* controller = Game::Controller(actor.get());
			const bool  fleeing = controller && controller->state && controller->IsFleeing();
			if (fleeing != watch.fleeing) {
				watch.fleeing = fleeing;
				const auto* profile = Find(member.id);
				if (fleeing) {
					Log::Info("[Traits] {} turns and runs at {:.0f}% health (confidence {})."sv, member.name,
						Game::Fraction(actor.get(), RE::ActorValue::kHealth) * 100.0f, profile ? profile->confidence : -1);
				} else {
					Log::Info("[Traits] {} stops running."sv, member.name);
				}
			}
		}
	}

	void InstallEvents()
	{
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESMagicEffectApplyEvent>(&applySink);
		}
	}

	void Reset()
	{
		known.clear();
		watches.clear();
		{
			const std::scoped_lock guard(ownersLock);
			owners.clear();
		}
		const std::scoped_lock guard(saidLock);
		saidAt.clear();
	}
}

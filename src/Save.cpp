#include "FWM/Save.h"
#include "FWM/Loadouts.h"

#include "FWM/Abilities.h"
#include "FWM/AbilityLink.h"
#include "FWM/Arts.h"
#include "FWM/Battle.h"
#include "FWM/Cooldowns.h"
#include "FWM/Host.h"
#include "FWM/Leveling.h"
#include "FWM/Log.h"
#include "FWM/OrdersCodec.h"
#include "FWM/Party.h"
#include "FWM/Windsteps.h"

namespace FWM::Save
{
	namespace
	{
		constexpr std::uint32_t kUniqueID = 'FWMO';
		constexpr std::uint32_t kOrders = 'ORDR';
		constexpr std::uint32_t kHands = 'HAND';
		constexpr std::uint32_t kBraced = 'BRCE';  // taunt armor on when the game was saved
		constexpr std::uint32_t kBracedVersion = 1;
		constexpr std::uint32_t kRejuvenated = 'RJUV';  // Rejuvenate's magicka on when the game was saved
		constexpr std::uint32_t kRejuvenatedVersion = 1;
		constexpr std::uint32_t kBoosts = 'BOST';  // every lift from an ability on when the game was saved: (actor, value, amount)
		constexpr std::uint32_t kBoostsVersion = 1;
		constexpr std::uint32_t kManual = 'AUTO';  // with the Paths add-on, the abilities each companion uses only when told
		constexpr std::uint32_t kManualVersion = 1;
		constexpr std::uint32_t kWaits = 'COOL';  // cooldowns still running when the game was saved
		constexpr std::uint32_t kLevels = 'LEVL';  // levels earned by fighting, and what they gave
		// Fight With Me's, first in every save made since the abilities moved to Path of the
		// Companion. Without it, the orders' ability settings in this save are Path of the
		// Companion's to take up.
		constexpr std::uint32_t kOrdersOnly = 'SPLT';

		bool ordersOnly{ false };  // this load's save has kOrdersOnly

		bool Write(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, const std::vector<std::uint8_t>& a_bytes)
		{
			return a_intfc->OpenRecord(a_type, a_version) && a_intfc->WriteRecordData(a_bytes.data(), static_cast<std::uint32_t>(a_bytes.size()));
		}

		// Flat (form id, amount) pairs: what a boost added, only ever read back to take it off again.
		std::vector<std::uint8_t> Pairs(const std::vector<std::pair<std::uint32_t, float>>& a_pairs)
		{
			std::vector<std::uint8_t> bytes(a_pairs.size() * 8);
			for (std::size_t i = 0; i < a_pairs.size(); ++i) {
				std::memcpy(bytes.data() + i * 8, &a_pairs[i].first, 4);
				std::memcpy(bytes.data() + i * 8 + 4, &a_pairs[i].second, 4);
			}
			return bytes;
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			Windsteps::Clear(); // No live illusion target or target hold crosses a save/load boundary.
			if (Host::Orders() && !Write(a_intfc, kOrdersOnly, 1, { 1 })) {
				Log::Error("[Save] Could not mark the save as one with orders only."sv);
			}
			const auto orders = Party::SavedOrders();
			if (!Write(a_intfc, kOrders, Codec::kVersion, Codec::Encode(orders))) {
				Log::Error("[Save] Could not write orders."sv);
				return;
			}
			// What they may fight with is Fight With Me's; Path of the Companion keeps it only on its own.
			const auto hands = Host::Steers() ? Party::SavedHands() : decltype(Party::SavedHands()){};
			if (Host::Steers()) {
				if (!Write(a_intfc, 'POOL', 1, Loadouts::Encode())) Log::Error("[Save] Could not write equipment pools."sv);
				if (!Write(a_intfc, kHands, Codec::kHandsVersion, Codec::EncodeHands(hands))) {
					Log::Error("[Save] Could not write the chosen spells."sv);
				}
			}
			if (!Host::Abilities()) {
				Log::Info("[Save] Orders for {} companion(s) saved{}."sv, orders.size(), hands.empty() ? ""s : std::format(", spells chosen for {}", hands.size()));
				return;
			}
			// Armor and quickened magicka added go where 0.2.0 already looks for them, so a save
			// taken back to it still sheds them; everything else (lowered values, other values) in
			// the newer record.
			auto positive = [](std::vector<std::pair<std::uint32_t, float>> a_pairs) {
				std::erase_if(a_pairs, [](const auto& a_pair) { return a_pair.second <= 0.0f; });
				return a_pairs;
			};
			const auto braced = positive(Abilities::Braced());
			const auto rejuvenated = positive(Abilities::Rejuvenated());
			if (!braced.empty() && !Write(a_intfc, kBraced, kBracedVersion, Pairs(braced))) {
				Log::Error("[Save] Could not write the armor still on."sv);
			}
			if (!rejuvenated.empty() && !Write(a_intfc, kRejuvenated, kRejuvenatedVersion, Pairs(rejuvenated))) {
				Log::Error("[Save] Could not write the quickened magicka still on."sv);
			}
			auto boosts = Abilities::Boosts();
			std::erase_if(boosts, [](const Abilities::SavedBoost& a_boost) {
				const auto value = static_cast<RE::ActorValue>(a_boost.value);
				return a_boost.amount > 0.0f && (value == RE::ActorValue::kDamageResist || value == RE::ActorValue::kMagickaRateMult);
			});
			if (!boosts.empty()) {
				std::vector<std::uint8_t> bytes(boosts.size() * 12);
				for (std::size_t i = 0; i < boosts.size(); ++i) {
					std::memcpy(bytes.data() + i * 12, &boosts[i].id, 4);
					std::memcpy(bytes.data() + i * 12 + 4, &boosts[i].value, 4);
					std::memcpy(bytes.data() + i * 12 + 8, &boosts[i].amount, 4);
				}
				if (!Write(a_intfc, kBoosts, kBoostsVersion, bytes)) {
					Log::Error("[Save] Could not write the ability lifts still on."sv);
				}
			}
			std::vector<std::uint8_t> manual;
			for (const auto& [id, each] : orders) {
				if (each.manual) {
					const auto at = manual.size();
					manual.resize(at + 8);
					std::memcpy(manual.data() + at, &id, 4);
					std::memcpy(manual.data() + at + 4, &each.manual, 4);
				}
			}
			if (!manual.empty() && !Write(a_intfc, kManual, kManualVersion, manual)) {
				Log::Error("[Save] Could not write the ability switches."sv);
			}
			auto waits = Party::SavedWaits();
			const auto arts = Arts::SavedWaits();
			waits.insert(waits.end(), arts.begin(), arts.end());
			if (!waits.empty() && !Write(a_intfc, kWaits, Cooldowns::kVersion, Cooldowns::Encode(waits))) {
				Log::Error("[Save] Could not write the cooldowns still running."sv);
			}
			if (const auto levels = Leveling::Encode(); !levels.empty() && !Write(a_intfc, kLevels, Leveling::kSaveVersion, levels)) {
				Log::Error("[Save] Could not write the levels earned."sv);
			}
			Log::Info("[Save] Orders for {} companion(s) saved{}{}{}{}."sv, orders.size(), hands.empty() ? ""s : std::format(", spells chosen for {}", hands.size()),
				braced.empty() ? ""s : std::format(", {} tank(s) braced", braced.size()), rejuvenated.empty() ? ""s : std::format(", {} rejuvenated", rejuvenated.size()),
				waits.empty() ? ""s : std::format(", {} cooldown(s) running", waits.size()));
		}

		std::uint32_t Resolve(SKSE::SerializationInterface* a_intfc, std::uint32_t a_id)
		{
			RE::FormID now = 0;
			return a_id && a_intfc->ResolveFormID(a_id, now) ? now : 0;
		}

		void LoadHands(SKSE::SerializationInterface* a_intfc, const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version)
		{
			std::vector<std::pair<std::uint32_t, Hands>> resolved;
			for (auto& [id, hands] : Codec::DecodeHands(a_bytes, a_version)) {
				const auto who = Resolve(a_intfc, id);
				if (!who) {
					continue;
				}
				// A spell from a plugin that is gone leaves that hand to them.
				resolved.emplace_back(who, Hands{ Resolve(a_intfc, hands[kLeftHand]), Resolve(a_intfc, hands[kRightHand]) });
			}
			Party::LoadHands(resolved);
			Log::Info("[Save] Spells chosen for {} companion(s) loaded."sv, resolved.size());
		}

		std::vector<std::pair<std::uint32_t, float>> ReadPairs(SKSE::SerializationInterface* a_intfc, const std::vector<std::uint8_t>& a_bytes)
		{
			std::vector<std::pair<std::uint32_t, float>> pairs;
			for (std::size_t at = 0; at + 8 <= a_bytes.size(); at += 8) {
				std::uint32_t id = 0;
				float         amount = 0.0f;
				std::memcpy(&id, a_bytes.data() + at, 4);
				std::memcpy(&amount, a_bytes.data() + at + 4, 4);
				if (const auto now = Resolve(a_intfc, id); now && amount > 0.0f && amount < 10000.0f) {
					pairs.emplace_back(now, amount);
				}
			}
			return pairs;
		}

		std::vector<std::pair<std::uint32_t, Orders>> ResolvedOrders(SKSE::SerializationInterface* a_intfc, const std::vector<std::uint8_t>& a_bytes,
			std::uint32_t a_version, std::size_t& a_dropped)
		{
			std::vector<std::pair<std::uint32_t, Orders>> resolved;
			for (auto& [id, orders] : Codec::Decode(a_bytes, a_version)) {
				if (const auto now = Resolve(a_intfc, id)) {
					resolved.emplace_back(now, orders);
				} else {
					++a_dropped;  // the follower's plugin is gone
				}
			}
			return resolved;
		}

		// One record of these, read back; false for a record that is not one of them.
		bool ReadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, const std::vector<std::uint8_t>& a_bytes)
		{
			if (a_type == kOrdersOnly) {
				ordersOnly = true;
				return true;
			}
			// Fight With Me reading a save from before the abilities moved out: what is Path of the
			// Companion's goes there. Levels earned by fighting still load here without it, to be
			// given back; cooldowns and switches go with the abilities.
			if (Host::Orders() && (a_type == kWaits || a_type == kManual || a_type == kLevels)) {
				const bool taken = AbilityLink::Adopt(a_intfc, a_type, a_version, a_bytes);
				if (taken || a_type != kLevels) {
					return true;
				}
			}
			// What they may fight with is Fight With Me's: beside it, a standalone save's pools stay unread here.
			if (a_type == 'POOL') { if (a_version == 1 && Host::Steers()) Loadouts::Decode(a_intfc,a_bytes); return true; }
			if (a_type == kHands) {
				if (Host::Steers()) {
					LoadHands(a_intfc, a_bytes, a_version);
				}
				return true;
			}
			if (a_type == kBraced) {
				Abilities::LoadBraced(ReadPairs(a_intfc, a_bytes));
				return true;
			}
			if (a_type == kRejuvenated) {
				Abilities::LoadRejuvenated(ReadPairs(a_intfc, a_bytes));
				return true;
			}
			if (a_type == kBoosts) {
				std::vector<Abilities::SavedBoost> saved;
				for (std::size_t at = 0; at + 12 <= a_bytes.size(); at += 12) {
					Abilities::SavedBoost boost;
					std::memcpy(&boost.id, a_bytes.data() + at, 4);
					std::memcpy(&boost.value, a_bytes.data() + at + 4, 4);
					std::memcpy(&boost.amount, a_bytes.data() + at + 8, 4);
					boost.id = Resolve(a_intfc, boost.id);
					if (boost.id && boost.value < static_cast<std::uint32_t>(RE::ActorValue::kTotal) && std::abs(boost.amount) < 10000.0f) {
						saved.push_back(boost);
					}
				}
				Abilities::LoadBoosts(saved);
				return true;
			}
			if (a_type == kLevels) {
				Leveling::Decode(a_bytes, a_version, [&](RE::FormID a_id) { return Resolve(a_intfc, a_id); });
				return true;
			}
			if (a_type == kWaits) {
				auto waits = Cooldowns::Decode(a_bytes, a_version);
				for (auto& wait : waits) {
					wait.id = Resolve(a_intfc, wait.id);
				}
				std::erase_if(waits, [](const Cooldowns::Wait& a_wait) { return a_wait.id == 0; });
				Party::LoadWaits(waits);
				Arts::LoadWaits(waits);
				Log::Info("[Save] {} cooldown(s) still running loaded."sv, waits.size());
				return true;
			}
			if (a_type == kManual) {
				for (std::size_t at = 0; at + 8 <= a_bytes.size(); at += 8) {
					std::uint32_t id = 0, mask = 0;
					std::memcpy(&id, a_bytes.data() + at, 4);
					std::memcpy(&mask, a_bytes.data() + at + 4, 4);
					if (const auto now = Resolve(a_intfc, id); now && Party::FindOrders(now)) {
						auto orders = *Party::FindOrders(now);
						orders.manual = mask;
						Party::SetOrders(now, orders);
					}
				}
				return true;
			}
			if (a_type != kOrders) {
				return false;
			}
			std::size_t dropped = 0;
			const auto  resolved = ResolvedOrders(a_intfc, a_bytes, a_version, dropped);
			Party::LoadOrders(resolved);
			Log::Info("[Save] Orders for {} companion(s) loaded{}."sv, resolved.size(),
				dropped ? std::format(", {} from removed plugins dropped", dropped) : ""s);
			// Made before the abilities moved out: their switches and thresholds in these orders are
			// Path of the Companion's to take up.
			if (Host::Orders() && !ordersOnly) {
				AbilityLink::Adopt(a_intfc, a_type, a_version, a_bytes);
			}
			return true;
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			ordersOnly = false;
			std::uint32_t type = 0, version = 0, length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				std::vector<std::uint8_t> bytes(length);
				if (length && a_intfc->ReadRecordData(bytes.data(), length) != length) {
					Log::Error("[Save] A record is short."sv);
					continue;
				}
				ReadRecord(a_intfc, type, version, bytes);
			}
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			Battle::Reset();
			Party::Clear();
            Loadouts::Clear();
		}
	}

	void Register()
	{
		auto* serialization = SKSE::GetSerializationInterface();
		if (!serialization) {
			Log::Error("[Save] Serialization interface unavailable; orders will not persist."sv);
			return;
		}
		serialization->SetUniqueID(kUniqueID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}

	void Write(SKSE::SerializationInterface* a_intfc)
	{
		OnSave(a_intfc);
	}

	bool Read(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, const std::vector<std::uint8_t>& a_bytes)
	{
		return ReadRecord(a_intfc, a_type, a_version, a_bytes);
	}

	void Adopt(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, const std::vector<std::uint8_t>& a_bytes)
	{
		if (a_type == kOrders) {
			std::size_t dropped = 0;
			const auto  resolved = ResolvedOrders(a_intfc, a_bytes, a_version, dropped);
			Party::AdoptOrders(resolved);
			Log::Info("[Save] An older save: the abilities' switches for {} companion(s) taken up from Fight With Me's orders."sv, resolved.size());
			return;
		}
		if (a_type == kWaits || a_type == kManual || a_type == kLevels) {
			ReadRecord(a_intfc, a_type, a_version, a_bytes);
			Log::Info("[Save] An older save: Fight With Me's {} taken up here."sv,
				a_type == kWaits ? "cooldowns"sv : a_type == kManual ? "ability switches"sv : "levels earned by fighting"sv);
		}
	}

	void Revert()
	{
		OnRevert(nullptr);
	}
}

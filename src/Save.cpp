#include "FWM/Save.h"

#include "FWM/Abilities.h"
#include "FWM/Arts.h"
#include "FWM/Battle.h"
#include "FWM/Cooldowns.h"
#include "FWM/Log.h"
#include "FWM/OrdersCodec.h"
#include "FWM/Party.h"

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
			const auto orders = Party::SavedOrders();
			if (!Write(a_intfc, kOrders, Codec::kVersion, Codec::Encode(orders))) {
				Log::Error("[Save] Could not write orders."sv);
				return;
			}
			const auto hands = Party::SavedHands();
			if (!Write(a_intfc, kHands, Codec::kHandsVersion, Codec::EncodeHands(hands))) {
				Log::Error("[Save] Could not write the chosen spells."sv);
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

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t type = 0, version = 0, length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				std::vector<std::uint8_t> bytes(length);
				if (length && a_intfc->ReadRecordData(bytes.data(), length) != length) {
					Log::Error("[Save] A record is short."sv);
					continue;
				}
				if (type == kHands) {
					LoadHands(a_intfc, bytes, version);
					continue;
				}
				if (type == kBraced) {
					Abilities::LoadBraced(ReadPairs(a_intfc, bytes));
					continue;
				}
				if (type == kRejuvenated) {
					Abilities::LoadRejuvenated(ReadPairs(a_intfc, bytes));
					continue;
				}
				if (type == kBoosts) {
					std::vector<Abilities::SavedBoost> saved;
					for (std::size_t at = 0; at + 12 <= bytes.size(); at += 12) {
						Abilities::SavedBoost boost;
						std::memcpy(&boost.id, bytes.data() + at, 4);
						std::memcpy(&boost.value, bytes.data() + at + 4, 4);
						std::memcpy(&boost.amount, bytes.data() + at + 8, 4);
						boost.id = Resolve(a_intfc, boost.id);
						if (boost.id && boost.value < static_cast<std::uint32_t>(RE::ActorValue::kTotal) && std::abs(boost.amount) < 10000.0f) {
							saved.push_back(boost);
						}
					}
					Abilities::LoadBoosts(saved);
					continue;
				}
				if (type == kWaits) {
					auto waits = Cooldowns::Decode(bytes, version);
					for (auto& wait : waits) {
						wait.id = Resolve(a_intfc, wait.id);
					}
					std::erase_if(waits, [](const Cooldowns::Wait& a_wait) { return a_wait.id == 0; });
					Party::LoadWaits(waits);
					Arts::LoadWaits(waits);
					Log::Info("[Save] {} cooldown(s) still running loaded."sv, waits.size());
					continue;
				}
				if (type == kManual) {
					for (std::size_t at = 0; at + 8 <= bytes.size(); at += 8) {
						std::uint32_t id = 0, mask = 0;
						std::memcpy(&id, bytes.data() + at, 4);
						std::memcpy(&mask, bytes.data() + at + 4, 4);
						if (const auto now = Resolve(a_intfc, id); now && Party::FindOrders(now)) {
							auto orders = *Party::FindOrders(now);
							orders.manual = mask;
							Party::SetOrders(now, orders);
						}
					}
					continue;
				}
				if (type != kOrders) {
					continue;
				}
				auto entries = Codec::Decode(bytes, version);
				std::size_t dropped = 0;
				std::vector<std::pair<std::uint32_t, Orders>> resolved;
				for (auto& [id, orders] : entries) {
					if (const auto now = Resolve(a_intfc, id)) {
						resolved.emplace_back(now, orders);
					} else {
						++dropped;  // the follower's plugin is gone
					}
				}
				Party::LoadOrders(resolved);
				Log::Info("[Save] Orders for {} companion(s) loaded{}."sv, resolved.size(),
					dropped ? std::format(", {} from removed plugins dropped", dropped) : ""s);
			}
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			Battle::Reset();
			Party::Clear();
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
}

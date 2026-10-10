#include "FWM/Save.h"

#include "FWM/Battle.h"
#include "FWM/Log.h"
#include "FWM/OrdersCodec.h"
#include "FWM/Party.h"

namespace FWM::Save
{
	namespace
	{
		constexpr std::uint32_t kUniqueID = 'FWMO';
		constexpr std::uint32_t kOrders = 'ORDR';

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			const auto orders = Party::SavedOrders();
			const auto bytes = Codec::Encode(orders);
			if (!a_intfc->OpenRecord(kOrders, Codec::kVersion) ||
				!a_intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
				Log::Error("[Save] Could not write orders."sv);
				return;
			}
			Log::Info("[Save] Orders for {} companion(s) saved."sv, orders.size());
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t type = 0, version = 0, length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type != kOrders) {
					continue;
				}
				std::vector<std::uint8_t> bytes(length);
				if (length && a_intfc->ReadRecordData(bytes.data(), length) != length) {
					Log::Error("[Save] Orders record is short."sv);
					continue;
				}
				auto entries = Codec::Decode(bytes, version);
				std::size_t dropped = 0;
				std::vector<std::pair<std::uint32_t, Orders>> resolved;
				for (auto& [id, orders] : entries) {
					RE::FormID now = 0;
					if (a_intfc->ResolveFormID(id, now)) {
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

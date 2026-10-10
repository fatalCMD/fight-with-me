#include "Plugin.h"

#include "FWM/Battle.h"
#include "FWM/Camera.h"
#include "FWM/Forms.h"
#include "FWM/Log.h"
#include "FWM/Menu.h"
#include "FWM/Overrides.h"
#include "FWM/Party.h"
#include "FWM/Save.h"
#include "FWM/Settings.h"
#include "FWM/TimeBend.h"
#include "FWM/WalkWithMe.h"

#include "FightWithMeAPI.h"

namespace
{
	struct PlayerUpdate
	{
		static void Thunk(RE::PlayerCharacter* a_player, float a_delta)
		{
			original(a_player, a_delta);
			FWM::Battle::Update(a_delta);
		}

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtable{ RE::PlayerCharacter::VTABLE[0] };
			original = vtable.write_vfunc(0xAD, Thunk);
			FWM::Log::Info("[Hook] Player update installed."sv);
		}

		static inline REL::Relocation<decltype(Thunk)> original;
	};

	class Interface final : public FightWithMeAPI::IFightWithMe
	{
	public:
		std::uint32_t GetVersion() const noexcept override { return FightWithMeAPI::INTERFACE_VERSION; }

		void OpenBattlePlan() override { FWM::Menu::Open(); }

		bool IsBattlePlanOpen() const override { return FWM::Menu::IsOpen(); }

		FightWithMeAPI::Role GetRole(std::uint32_t a_actorFormID) const override
		{
			const auto* orders = FWM::Party::FindOrders(a_actorFormID);
			return orders ? static_cast<FightWithMeAPI::Role>(orders->role) : FightWithMeAPI::Role::kNone;
		}
	} api;

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}
		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			FWM::WalkWithMe::Connect();
			FWM::Camera::RegisterSmoothCam();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			FWM::Forms::Resolve();
			FWM::TimeBend::Register();
			FWM::Overrides::Install();
			PlayerUpdate::Install();
			FWM::Camera::Install();
			FWM::Camera::RequestSmoothCam();
			FWM::Battle::InstallEvents();
			FWM::Menu::Register();
			break;
		case SKSE::MessagingInterface::kNewGame:
		case SKSE::MessagingInterface::kPreLoadGame:
			FWM::Battle::Reset();
			break;
		default:
			break;
		}
	}
}

extern "C" FIGHTWITHME_API FightWithMeAPI::IFightWithMe* FightWithMe_GetInterface(std::uint32_t a_version)
{
	return a_version == FightWithMeAPI::INTERFACE_VERSION ? &api : nullptr;
}

extern "C" FIGHTWITHME_API bool FightWithMe_OpenBattlePlan()
{
	FWM::Menu::Open();
	return FWM::Menu::Available();
}

EXTERN_C [[maybe_unused]] __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	FWM::Log::Setup();
	FWM::Log::Info("{} {} loading."sv, Plugin::DISPLAY_NAME, Plugin::VERSION.string("."sv));

	// false: CommonLibSSE-NG's default re-opens the log and replaces its format.
	SKSE::Init(a_skse, false);

	if (REL::Module::IsVR()) {
		FWM::Log::Error("Skyrim VR is not supported."sv);
		return false;
	}

	FWM::Settings::Load();
	FWM::Save::Register();

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		FWM::Log::Error("Messaging interface unavailable."sv);
		return false;
	}
	return true;
}

EXTERN_C [[maybe_unused]] __declspec(dllexport) constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData data;
	data.PluginName(Plugin::NAME);
	data.PluginVersion(Plugin::VERSION);
	data.AuthorName("Fight With Me");
	data.UsesAddressLibrary();
	// The combat controller and selector fields used here sit before the point
	// where 1.6.629 changed CombatController, and the rest goes through
	// CommonLibSSE-NG's versioned accessors.
	data.UsesNoStructs();
	return data;
}();

EXTERN_C [[maybe_unused]] __declspec(dllexport) bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
	a_info->name = SKSEPlugin_Version.pluginName;
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

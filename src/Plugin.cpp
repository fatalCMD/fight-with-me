#include "Plugin.h"

#include "FWM/AbilityLink.h"
#include "FWM/Battle.h"
#include "FWM/Camera.h"
#include "FWM/Forms.h"
#include "FWM/Log.h"
#include "FWM/Menu.h"
#include "FWM/Overrides.h"
#include "FWM/Party.h"
#include "FWM/Save.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"
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

	// The words in the player's language: the INI's choice, or the game's own. Before anything
	// registers or draws, and never again: changing it takes a restart.
	void LoadText()
	{
		std::string language = FWM::Settings::Get().language;
		if (language.empty()) {
			if (auto* setting = RE::GetINISetting("sLanguage:General"); setting && setting->GetType() == RE::Setting::Type::kString && setting->GetString()) {
				language = setting->GetString();
			}
		}
		language = FWM::Text::Upper(language.empty() ? "ENGLISH"sv : std::string_view(language));
		std::error_code ec;
		const auto      folder = std::filesystem::current_path(ec) / L"Data" / L"SKSE" / L"Plugins" / L"FightWithMe" / L"Translations";
		const auto      loaded = FWM::Text::Load(folder, language);
		if (loaded.files.empty()) {
			FWM::Log::Info("[Text] {}: no translation files, so English."sv, language);
			return;
		}
		std::string files;
		for (const auto& file : loaded.files) {
			files += (files.empty() ? "" : ", ") + file;
		}
		FWM::Log::Info("[Text] {}: {} lines from {}{}."sv, language, loaded.entries, files,
			loaded.unreadable ? std::format("; {} line(s) could not be read (no tab, or not UTF-8)", loaded.unreadable) : ""s);
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}
		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostLoad:
			FWM::WalkWithMe::Connect();
			FWM::AbilityLink::Connect();
			FWM::Camera::RegisterSmoothCam();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			LoadText();
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

extern "C" FIGHTWITHME_API bool FightWithMe_InBattle()
{
	return FWM::Battle::PartyFighting();
}

// Thread-safe settings snapshot, also read by Paths' render callbacks.
extern "C" FIGHTWITHME_API int FightWithMe_GetTheme()
{
	return FWM::Settings::Get().uiTheme;
}

// Called on the main thread by Paths' queued settings update.
extern "C" FIGHTWITHME_API bool FightWithMe_SetTheme(int theme)
{
    return FWM::Settings::ApplyTheme(theme);
}

extern "C" FIGHTWITHME_API std::uint32_t FightWithMe_GetMark(std::uint32_t a_actorFormID)
{
	return FWM::Battle::MarkOf(a_actorFormID).native_handle();
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

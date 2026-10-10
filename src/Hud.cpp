#include "FWM/Hud.h"

#include "FWM/Log.h"
#include "FWM/Settings.h"

namespace FWM::Hud
{
	namespace
	{
		std::vector<std::string> hidden;
	}

	void Hide()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !hidden.empty() || !Settings::Get().hideHud) {
			return;
		}
		for (auto& [name, entry] : ui->menuMap) {
			const std::string_view key(name.c_str());
			// The cursor stays: Menu Framework's window may be steered with it.
			if (!entry.menu || !entry.menu->uiMovie || key == RE::CursorMenu::MENU_NAME) {
				continue;
			}
			if (entry.menu->uiMovie->GetVisible()) {
				entry.menu->uiMovie->SetVisible(false);
				hidden.emplace_back(key);
			}
		}
	}

	void Restore()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			hidden.clear();
			return;
		}
		for (const auto& name : hidden) {
			const auto it = ui->menuMap.find(RE::BSFixedString(name.c_str()));
			if (it != ui->menuMap.end() && it->second.menu && it->second.menu->uiMovie) {
				it->second.menu->uiMovie->SetVisible(true);
			}
		}
		hidden.clear();
	}
}

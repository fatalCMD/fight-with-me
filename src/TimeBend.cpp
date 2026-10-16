#include "FWM/TimeBend.h"

#include "FWM/Forms.h"
#include "FWM/Log.h"
#include "FWM/Settings.h"

namespace FWM::TimeBend
{
	namespace
	{
		constexpr auto kPauseMenu = "FightWithMePause"sv;

		// An invisible native menu lets Skyrim own the pause count and its cleanup.
		class PauseMenu final : public RE::IMenu
		{
		public:
			PauseMenu()
			{
				menuFlags.set(RE::UI_MENU_FLAGS::kPausesGame);
				depthPriority = 0;
			}

			static RE::IMenu* Create() { return new PauseMenu(); }
		};

		// What KeepClear lifted, to give back on close; and whether the log has said so.
		bool unfroze{ false };
		bool unblurred{ false };
		bool toldFreeze{ false };
		bool toldBlur{ false };

		bool  wanted{ false };
		bool  active{ false };
		bool  paused{ false };
		bool  effect{ false };
		float progress{ 0.0f };  // 0 = normal time, 1 = fully slowed
		float before{ 1.0f };    // multiplier when the plan opened
		float target{ 1.0f };
		float lastSet{ -1.0f };

		void SetMultiplier(float a_value)
		{
			if (auto* timer = RE::BSTimer::GetSingleton()) {
				timer->SetGlobalTimeMultiplier(a_value, true);
				lastSet = a_value;
			}
		}

		void ShowPause(bool a_show)
		{
			if (paused == a_show) {
				return;
			}
			if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
				queue->AddMessage(kPauseMenu, a_show ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide, nullptr);
				paused = a_show;
			}
		}

		void StartEffect()
		{
			const auto& forms = Forms::Get();
			if (!Settings::Get().timeEffect || effect) {
				return;
			}
			// Leave the screen alone if something else already slowed time (the shout).
			if (before < 0.99f) {
				return;
			}
			if (forms.imodIntro) {
				RE::ImageSpaceModifierInstanceForm::Trigger(forms.imodIntro, 1.0f, nullptr);
			}
			if (forms.imodHold) {
				RE::ImageSpaceModifierInstanceForm::Trigger(forms.imodHold, 1.0f, nullptr);
			}
			effect = true;
		}

		void StopEffect()
		{
			if (!effect) {
				return;
			}
			const auto& forms = Forms::Get();
			if (forms.imodHold) {
				RE::ImageSpaceModifierInstanceForm::Stop(forms.imodHold);
			}
			if (forms.imodIntro) {
				RE::ImageSpaceModifierInstanceForm::Stop(forms.imodIntro);
			}
			effect = false;
		}
	}

	void Register()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->Register(kPauseMenu, PauseMenu::Create);
		}
	}

	void Begin()
	{
		wanted = true;
		const auto& settings = Settings::Get();
		if (settings.stopTime) {
			ShowPause(true);
			return;
		}
		if (!active) {
			before = RE::BSTimer::QGlobalTimeMultiplier();
			target = before * settings.timeScale;
			progress = 0.0f;
			active = true;
			StartEffect();
		}
	}

	void End()
	{
		wanted = false;
		ShowPause(false);
	}

	void Update()
	{
		if (!active) {
			return;
		}
		auto*       timer = RE::BSTimer::GetSingleton();
		const float real = timer ? std::clamp(timer->realTimeDelta, 0.0f, 0.1f) : 0.016f;
		const float ease = Settings::Get().easeSeconds;
		const float step = ease > 0.0f ? real / ease : 1.0f;

		// Something else took over the clock (a shout ending, another mod): let go.
		if (lastSet >= 0.0f && std::fabs(RE::BSTimer::QGlobalTimeMultiplier() - lastSet) > 0.001f &&
			std::fabs(RE::BSTimer::QGlobalTimeMultiplierTarget() - lastSet) > 0.001f) {
			Log::Info("[Time] The time multiplier was changed elsewhere; leaving it alone."sv);
			active = false;
			lastSet = -1.0f;
			StopEffect();
			return;
		}

		progress = std::clamp(progress + (wanted ? step : -step), 0.0f, 1.0f);
		const float t = progress * progress * (3.0f - 2.0f * progress);
		SetMultiplier(before + (target - before) * t);

		if (!wanted && progress <= 0.0f) {
			SetMultiplier(before);
			active = false;
			lastSet = -1.0f;
			StopEffect();
		}
	}

	void KeepClear()
	{
		// Menu Framework sets this once when its lock starts and clears it when the lock ends,
		// so clearing it here holds until then.
		if (auto* main = RE::Main::GetSingleton(); main && main->GetRuntimeData().freezeTime) {
			main->GetRuntimeData().freezeTime = false;
			unfroze = true;
			if (!toldFreeze) {
				toldFreeze = true;
				Log::Info("[Time] The world was frozen under the battle plan (Menu Framework's FreezeTimeOnMenu); lifted while the plan is open."sv);
			}
		}
		// The menu blur is one image-space effect shared by everyone who counts on it. The count
		// is left alone; only the effect is stopped, and restarted on close if still wanted.
		if (auto* blur = RE::UIBlurManager::GetSingleton(); blur && blur->blurCount > 0 && blur->blurEffect) {
			RE::ImageSpaceModifierInstanceForm::Stop(blur->blurEffect);
			unblurred = true;
			if (!toldBlur) {
				toldBlur = true;
				Log::Info("[Time] The menu blur was on under the battle plan (Menu Framework's BlurBackgroundOnMenu or another menu); kept off while the plan is open."sv);
			}
		}
	}

	void HandBack(bool a_menuStillOpen)
	{
		if (unblurred) {
			unblurred = false;
			if (auto* blur = RE::UIBlurManager::GetSingleton(); blur && blur->blurCount > 0 && blur->blurEffect) {
				RE::ImageSpaceModifierInstanceForm::Trigger(blur->blurEffect, 1.0f, nullptr);  // the strength the game turns it on with
			}
		}
		if (unfroze) {
			unfroze = false;
			if (a_menuStillOpen) {
				if (auto* main = RE::Main::GetSingleton()) {
					main->GetRuntimeData().freezeTime = true;
				}
			}
		}
	}

	bool Active() noexcept
	{
		return active || paused;
	}
}

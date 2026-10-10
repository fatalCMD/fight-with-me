#include "FWM/Settings.h"

#include "FWM/Log.h"

namespace FWM::Settings
{
	namespace
	{
		// Every version ever applied stays alive, so a reference from Get() taken on
		// another thread can never dangle. Settings change a handful of times a session.
		const Data                         defaults{};
		std::mutex                         versionsLock;
		std::vector<std::unique_ptr<Data>> versions;
		std::atomic<const Data*>           current{ &defaults };

		std::mutex                                     submitLock;
		std::optional<std::pair<std::uint64_t, Data>>  pending;
		bool                                           taskQueued{ false };
		std::atomic<std::uint64_t>                     requested{ 0 }, applied{ 0 };
		std::atomic<bool>                              saveFailed{ false };

		// Wide profile calls take the path as UTF-16, so the Skyrim folder can be
		// named in any script. Under MO2 the virtual Data path resolves to whichever
		// mod provides the file.
		[[nodiscard]] std::wstring IniPath()
		{
			std::error_code ec;
			const auto      root = std::filesystem::current_path(ec);
			return (root / L"Data" / L"SKSE" / L"Plugins" / L"FightWithMe.ini").wstring();
		}

		struct Reader
		{
			std::wstring path;

			[[nodiscard]] std::wstring Raw(const wchar_t* a_section, const wchar_t* a_key) const
			{
				wchar_t buffer[256]{};
				::GetPrivateProfileStringW(a_section, a_key, L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
				std::wstring value = buffer;
				// Trailing comments are allowed: "fFoo = 1.0 ; note".
				if (const auto semi = value.find(L';'); semi != std::wstring::npos) {
					value.resize(semi);
				}
				while (!value.empty() && iswspace(value.back())) {
					value.pop_back();
				}
				return value;
			}

			void Bool(const wchar_t* a_section, const wchar_t* a_key, bool& a_value) const
			{
				const auto raw = Raw(a_section, a_key);
				if (!raw.empty()) {
					a_value = raw == L"1" || _wcsicmp(raw.c_str(), L"true") == 0;
				}
			}

			void Float(const wchar_t* a_section, const wchar_t* a_key, float& a_value, float a_min, float a_max) const
			{
				const auto raw = Raw(a_section, a_key);
				if (raw.empty()) {
					return;
				}
				wchar_t*    end = nullptr;
				const float value = std::wcstof(raw.c_str(), &end);
				if (end != raw.c_str() && std::isfinite(value)) {
					a_value = std::clamp(value, a_min, a_max);
				}
			}

			void Int(const wchar_t* a_section, const wchar_t* a_key, int& a_value, int a_min, int a_max) const
			{
				const auto raw = Raw(a_section, a_key);
				if (raw.empty()) {
					return;
				}
				wchar_t*   end = nullptr;
				const long value = std::wcstol(raw.c_str(), &end, 0);
				if (end != raw.c_str()) {
					a_value = std::clamp(static_cast<int>(value), a_min, a_max);
				}
			}

			void Key(const wchar_t* a_section, const wchar_t* a_key, std::uint32_t& a_value) const
			{
				int value = static_cast<int>(a_value);
				Int(a_section, a_key, value, -1, 0xFFFF);
				a_value = value < 0 ? 0 : static_cast<std::uint32_t>(value);
			}

			void String(const wchar_t* a_section, const wchar_t* a_key, std::string& a_value) const
			{
				const auto raw = Raw(a_section, a_key);
				if (raw.empty()) {
					return;
				}
				const int   size = ::WideCharToMultiByte(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0, nullptr, nullptr);
				std::string out(static_cast<std::size_t>(size), '\0');
				::WideCharToMultiByte(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), out.data(), size, nullptr, nullptr);
				a_value = std::move(out);
			}
		};

		// Writes one key at a time, in place: comments and keys we do not know survive,
		// and nothing is renamed or moved, which MO2's virtual file system handles badly.
		struct Writer
		{
			std::wstring path;
			bool         ok{ true };

			void Put(const wchar_t* a_section, const wchar_t* a_key, const std::wstring& a_value)
			{
				ok = ::WritePrivateProfileStringW(a_section, a_key, a_value.c_str(), path.c_str()) != 0 && ok;
			}

			void Bool(const wchar_t* a_section, const wchar_t* a_key, bool& a_value) { Put(a_section, a_key, a_value ? L"true" : L"false"); }
			void Float(const wchar_t* a_section, const wchar_t* a_key, float& a_value, float, float) { Put(a_section, a_key, std::format(L"{}", a_value)); }
			void Int(const wchar_t* a_section, const wchar_t* a_key, int& a_value, int, int) { Put(a_section, a_key, std::format(L"{}", a_value)); }
			void Key(const wchar_t* a_section, const wchar_t* a_key, std::uint32_t& a_value) { Put(a_section, a_key, std::format(L"{}", a_value)); }

			void String(const wchar_t* a_section, const wchar_t* a_key, std::string& a_value)
			{
				const int    size = ::MultiByteToWideChar(CP_UTF8, 0, a_value.c_str(), static_cast<int>(a_value.size()), nullptr, 0);
				std::wstring out(static_cast<std::size_t>(size), L'\0');
				::MultiByteToWideChar(CP_UTF8, 0, a_value.c_str(), static_cast<int>(a_value.size()), out.data(), size);
				Put(a_section, a_key, out);
			}
		};

		// The one list of keys, read and written alike.
		template <class V>
		void Visit(V& v, Data& d)
		{
			auto& t = d.tuning;

			v.Bool(L"General", L"bEnabled", d.enabled);
			v.Float(L"General", L"fStyleWeight", d.styleWeight, 0.0f, 1.0f);
			v.Float(L"General", L"fDecisionInterval", d.planInterval, 0.1f, 2.0f);
			v.Bool(L"General", L"bGuessRoles", d.guessRoles);
			v.Bool(L"General", L"bLeaveEmptyFights", d.leaveEmptyFights);
			v.Float(L"General", L"fLeaveAfter", d.leaveAfter, 0.5f, 15.0f);

			v.Key(L"BattlePlan", L"iKey", d.planKey);
			v.Key(L"BattlePlan", L"iGamepadKey", d.planGamepadKey);
			v.Key(L"Orders", L"iAttack", d.orderKeys[0]);
			v.Key(L"Orders", L"iToMe", d.orderKeys[1]);
			v.Key(L"Orders", L"iHoldBack", d.orderKeys[2]);
			v.Key(L"Orders", L"iAsYouWere", d.orderKeys[3]);
			v.Float(L"BattlePlan", L"fTimeScale", d.timeScale, 0.02f, 1.0f);
			v.Bool(L"BattlePlan", L"bStopTime", d.stopTime);
			v.Float(L"BattlePlan", L"fEaseSeconds", d.easeSeconds, 0.0f, 2.0f);
			v.Bool(L"BattlePlan", L"bTimeEffect", d.timeEffect);
			v.Bool(L"BattlePlan", L"bHideHUD", d.hideHud);
			v.Bool(L"BattlePlan", L"bCameraGlide", d.cameraGlide);
			v.Float(L"BattlePlan", L"fPortraitDistance", d.portraitDistance, 50.0f, 400.0f);
			v.Float(L"BattlePlan", L"fCameraDistance", d.cameraDistance, 120.0f, 700.0f);
			v.Float(L"BattlePlan", L"fCameraHeight", d.cameraHeight, 0.0f, 300.0f);
			v.Float(L"BattlePlan", L"fScale", d.uiScale, 0.6f, 2.0f);

			v.Bool(L"Taunt", L"bEnabled", d.taunt);
			v.Bool(L"Taunt", L"bOpener", d.tauntOpener);
			v.Bool(L"Taunt", L"bShieldWall", d.shieldWall);
			v.Float(L"Taunt", L"fChance", d.tauntChance, 0.02f, 1.0f);
			v.Float(L"Taunt", L"fCheckSeconds", d.tauntCheckSeconds, 0.5f, 10.0f);
			v.Float(L"Taunt", L"fRadiusBase", t.tauntRadiusBase, 100.0f, 4000.0f);
			v.Float(L"Taunt", L"fRadiusPerSkill", t.tauntRadiusPerSkill, 0.0f, 50.0f);
			v.Float(L"Taunt", L"fRadiusMax", t.tauntRadiusMax, 100.0f, 6000.0f);
			v.Float(L"Taunt", L"fReachDivisor", t.tauntReachDivisor, 1.0f, 50.0f);
			v.Float(L"Taunt", L"fFalloffPerLevel", t.tauntFalloffPerLevel, 0.0f, 1.0f);
			v.Int(L"Taunt", L"iFalloffLevels", t.tauntFalloffLevels, 0, 50);
			v.Int(L"Taunt", L"iBossLevelBonus", t.bossLevelBonus, 0, 50);
			v.Int(L"Taunt", L"iCowedLevelGap", t.cowedLevelGap, 1, 100);
			v.Float(L"Taunt", L"fCowedHoldMult", t.cowedHoldMult, 1.0f, 5.0f);
			v.Float(L"Taunt", L"fHoldBase", t.holdBase, 0.5f, 60.0f);
			v.Float(L"Taunt", L"fHoldPerLevel", t.holdPerLevel, 0.0f, 1.0f);
			v.Float(L"Taunt", L"fCooldownBase", t.cooldownBase, 1.0f, 120.0f);
			v.Float(L"Taunt", L"fCooldownPerLevel", t.cooldownPerLevel, 0.0f, 2.0f);
			v.Float(L"Taunt", L"fCooldownMin", t.cooldownMin, 0.5f, 120.0f);
			v.Float(L"Taunt", L"fStaminaCost", t.tauntStamina, 0.0f, 500.0f);
			v.Float(L"Taunt", L"fHoldBreakDistance", t.holdBreakDistance, 500.0f, 10000.0f);
			v.Float(L"Taunt", L"fRangedReach", t.tauntRangedReach, 500.0f, 8000.0f);
			v.Float(L"Taunt", L"fGlowSeconds", d.glowSeconds, 0.0f, 10.0f);
			v.String(L"Taunt", L"sGlowShader", d.glowShader);
			v.String(L"Taunt", L"sSound", d.tauntSound);
			v.String(L"Taunt", L"sIdleShield", d.idleShield);
			v.String(L"Taunt", L"sIdleWeapon", d.idleWeapon);
			v.String(L"Taunt", L"sIdleCry", d.idleCry);

			v.Bool(L"Heal", L"bEnabled", d.heal);
			v.Bool(L"Heal", L"bFallback", d.healFallback);
			v.String(L"Heal", L"sFallbackSpell", d.fallbackSpell);
			v.Float(L"Heal", L"fSelfThreshold", t.healSelfThreshold, 0.05f, 0.95f);
			v.Float(L"Heal", L"fCooldown", t.healCooldown, 10.0f, 120.0f);
			v.Float(L"Heal", L"fRange", t.healRange, 200.0f, 6000.0f);
			v.Float(L"Heal", L"fReviveReach", t.reviveReach, 300.0f, 8000.0f);
			v.Float(L"Heal", L"fReviveDistance", t.reviveDistance, 120.0f, 600.0f);
			v.Float(L"Heal", L"fConcentrationSeconds", d.concentrationSeconds, 0.5f, 10.0f);

			v.Float(L"Targeting", L"fEngageRange", t.engageRange, 500.0f, 10000.0f);
			v.Float(L"Targeting", L"fGuardRadius", t.guardRadius, 100.0f, 5000.0f);
			v.Float(L"Targeting", L"fHoldBackRadius", t.holdBackRadius, 100.0f, 5000.0f);
			v.Float(L"Targeting", L"fWithMeRadius", t.withMeRadius, 150.0f, 1500.0f);
			v.Float(L"Targeting", L"fDefendRange", t.defendRange, 100.0f, 1500.0f);
			v.Float(L"Targeting", L"fStickiness", t.stickiness, 0.0f, 500.0f);
			v.Float(L"Targeting", L"fSpreadPenalty", t.spreadPenalty, 0.0f, 500.0f);
			v.Float(L"Targeting", L"fDistanceWeight", t.distanceWeight, 0.0f, 1.0f);
			v.Bool(L"Targeting", L"bLeash", d.guardLeash);
			v.Float(L"Targeting", L"fHoldBackLeash", d.holdBackLeash, 300.0f, 4000.0f);

			v.String(L"TimeEffect", L"sIntro", d.imodIntro);
			v.String(L"TimeEffect", L"sHold", d.imodHold);

			v.Bool(L"Interface", L"bNotices", d.notices);
			v.Bool(L"Interface", L"bSounds", d.sounds);
			v.Bool(L"Interface", L"bTooltips", d.tooltips);

			v.Bool(L"Debug", L"bLogDecisions", d.logDecisions);
		}

		void Sanitize(Data& a_data)
		{
			auto& t = a_data.tuning;
			t.tauntRadiusMax = std::max(t.tauntRadiusMax, t.tauntRadiusBase);
			t.cooldownMin = std::min(t.cooldownMin, t.cooldownBase);
		}

		void Publish(Data a_data)
		{
			Sanitize(a_data);
			const std::scoped_lock guard(versionsLock);
			versions.push_back(std::make_unique<Data>(std::move(a_data)));
			current.store(versions.back().get(), std::memory_order_release);
		}
	}

	void Load()
	{
		const Reader r{ IniPath() };
		const bool   found = std::filesystem::exists(r.path);
		Data         d;
		Visit(r, d);
		Publish(d);

		const auto& live = Get();
		Log::Info("Settings {}: plan key {}, time x{:.2f}{}, taunt {}, heal {}, style weight {:.2f}."sv,
			found ? "read from FightWithMe.ini"sv : "defaulted (FightWithMe.ini not found)"sv,
			live.planKey, live.timeScale, live.stopTime ? " (stopped)"sv : ""sv, live.taunt, live.heal, live.styleWeight);
	}

	const Data& Get() noexcept
	{
		return *current.load(std::memory_order_acquire);
	}

	Data Defaults()
	{
		return {};
	}

	bool Apply(const Data& a_data)
	{
		Publish(a_data);
		Writer w{ IniPath() };
		Data   copy = Get();
		Visit(w, copy);
		if (!w.ok) {
			Log::Error("[Settings] Could not write FightWithMe.ini (error {})."sv, ::GetLastError());
		}
		return w.ok;
	}

	std::uint64_t Submit(const Data& a_data)
	{
		bool          startTask = false;
		std::uint64_t revision = 0;
		{
			const std::scoped_lock guard(submitLock);
			revision = ++requested;
			pending = std::make_pair(revision, a_data);
			startTask = !taskQueued;
			taskQueued = true;
		}
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			const std::scoped_lock guard(submitLock);
			taskQueued = false;
			saveFailed = true;
			return revision;
		}
		if (startTask) {
			tasks->AddTask([] {
				for (;;) {
					std::optional<std::pair<std::uint64_t, Data>> update;
					{
						const std::scoped_lock guard(submitLock);
						if (!pending) {
							taskQueued = false;
							return;
						}
						update = std::move(pending);
						pending.reset();
					}
					saveFailed = !Apply(update->second);
					applied = update->first;
				}
			});
		}
		return revision;
	}

	std::uint64_t AppliedRevision() noexcept
	{
		return applied.load();
	}

	std::uint64_t RequestedRevision() noexcept
	{
		return requested.load();
	}

	bool LastSaveFailed() noexcept
	{
		return saveFailed.load();
	}
}

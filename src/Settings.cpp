#include "FWM/Settings.h"

#include "FWM/Log.h"
#include "FWM/UITheme.h"

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
		std::atomic<const Data*>           chosen{ &defaults };   // the player's own, before the paths' lock
		std::atomic<const Data*>           shipped{ &defaults };  // FightWithMe.ini alone
		std::atomic<bool>                  rolesLocked{ false };

		std::mutex                                     submitLock;
		std::optional<std::pair<std::uint64_t, Data>>  pending;
		bool                                           taskQueued{ false };
		std::atomic<std::uint64_t>                     requested{ 0 }, applied{ 0 };
		std::atomic<bool>                              saveFailed{ false };

		// Wide profile calls take the path as UTF-16, so the Skyrim folder can be
		// named in any script. Under MO2 the virtual Data path resolves to whichever
		// mod provides the file.
		[[nodiscard]] std::filesystem::path PluginFolder()
		{
			std::error_code ec;
			return std::filesystem::current_path(ec) / L"Data" / L"SKSE" / L"Plugins";
		}

		// The shipped file: defaults and what each key does. Every update replaces it.
		[[nodiscard]] std::wstring IniPath()
		{
			return (PluginFolder() / L"FightWithMe.ini").wstring();
		}

		// What the player changed in game. Never shipped, so an update cannot reset it;
		// under MO2 a new file lands in overwrite. Holds only keys that differ from
		// FightWithMe.ini, so a changed default in a later version still arrives.
		[[nodiscard]] std::wstring UserIniPath()
		{
			return (PluginFolder() / L"FightWithMe_user.ini").wstring();
		}

		struct Reader
		{
			std::wstring path;

			[[nodiscard]] std::wstring Raw(const wchar_t* a_section, const wchar_t* a_key) const
			{
				return Saved(a_section, a_key).value_or(std::wstring{});
			}

			// The value as written, or nothing when the key is not in the file at all.
			[[nodiscard]] std::optional<std::wstring> Saved(const wchar_t* a_section, const wchar_t* a_key) const
			{
				static constexpr wchar_t kAbsent[] = L"\x1";
				wchar_t                  buffer[256]{};
				::GetPrivateProfileStringW(a_section, a_key, kAbsent, buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
				if (std::wcscmp(buffer, kAbsent) == 0) {
					return std::nullopt;
				}
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

		// Every key as it would be written, in Visit order, so two versions can be compared
		// line by line.
		struct Lines
		{
			struct Line
			{
				const wchar_t* section;
				const wchar_t* key;
				std::wstring   value;
			};
			std::vector<Line> lines;

			void Put(const wchar_t* a_section, const wchar_t* a_key, std::wstring a_value)
			{
				lines.push_back({ a_section, a_key, std::move(a_value) });
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

			v.Bool(L"Sight", L"bDrawWeapons", d.drawOnSight);
			v.Bool(L"Sight", L"bAutoBattle", d.autoBattle);
			v.Float(L"Sight", L"fRange", d.sightRange, 500.0f, 6000.0f);

			v.Key(L"BattlePlan", L"iKey", d.planKey.key);
			v.Key(L"BattlePlan", L"iKeyModifier", d.planKey.modifier);
			v.Key(L"BattlePlan", L"iGamepadKey", d.planGamepadKey);
			v.Key(L"Orders", L"iAttack", d.orderKeys[0].key);
			v.Key(L"Orders", L"iAttackModifier", d.orderKeys[0].modifier);
			v.Key(L"Orders", L"iToMe", d.orderKeys[1].key);
			v.Key(L"Orders", L"iToMeModifier", d.orderKeys[1].modifier);
			v.Key(L"Orders", L"iHoldBack", d.orderKeys[2].key);
			v.Key(L"Orders", L"iHoldBackModifier", d.orderKeys[2].modifier);
			v.Key(L"Orders", L"iAsYouWere", d.orderKeys[3].key);
			v.Key(L"Orders", L"iAsYouWereModifier", d.orderKeys[3].modifier);
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
			v.Float(L"BattlePlan", L"fOrbitSpeed", d.orbitSpeed, 0.2f, 3.0f);
			v.Key(L"BattlePlan", L"iOverheadKey", d.overheadKey.key);
			v.Key(L"BattlePlan", L"iOverheadKeyModifier", d.overheadKey.modifier);
			v.Key(L"BattlePlan", L"iOverheadGamepadKey", d.overheadGamepadKey);
			v.Float(L"BattlePlan", L"fOverheadPitch", d.overheadPitch, 35.0f, 88.0f);
			v.Bool(L"BattlePlan", L"bCutaway", d.cutaway);
			v.Float(L"BattlePlan", L"fCutawayMargin", d.cutawayMargin, 0.0f, 400.0f);

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
			v.Float(L"Taunt", L"fHoldSeconds", t.holdSeconds, 1.0f, 120.0f);
			v.Float(L"Taunt", L"fHoldSecondsLevel30", t.holdSecondsAt30, 1.0f, 120.0f);
			v.Float(L"Taunt", L"fHoldSecondsLevel60", t.holdSecondsAt60, 1.0f, 120.0f);
			v.Float(L"Taunt", L"fCooldownBase", t.cooldownBase, 1.0f, 120.0f);
			v.Float(L"Taunt", L"fCooldownPerLevel", t.cooldownPerLevel, 0.0f, 2.0f);
			v.Float(L"Taunt", L"fCooldownMin", t.cooldownMin, 0.5f, 120.0f);
			v.Float(L"Taunt", L"fStaminaCost", t.tauntStamina, 0.0f, 500.0f);
			v.Float(L"Taunt", L"fHoldBreakDistance", t.holdBreakDistance, 500.0f, 10000.0f);
			v.Float(L"Taunt", L"fRangedReach", t.tauntRangedReach, 500.0f, 8000.0f);
			v.Float(L"Taunt", L"fArmorBase", t.tauntArmorBase, 0.0f, 500.0f);
			v.Float(L"Taunt", L"fArmorPerLevel", t.tauntArmorPerLevel, 0.0f, 20.0f);
			v.Float(L"Taunt", L"fArmorMax", t.tauntArmorMax, 0.0f, 1000.0f);
			v.Bool(L"Taunt", L"bBrace", d.brace);
			v.Float(L"Taunt", L"fGlowSeconds", d.glowSeconds, 0.0f, 10.0f);
			v.String(L"Taunt", L"sSound", d.tauntSound);
			v.String(L"Taunt", L"sIdleShield", d.idleShield);
			v.String(L"Taunt", L"sIdleWeapon", d.idleWeapon);
			v.String(L"Taunt", L"sIdleCry", d.idleCry);

			v.Bool(L"Heal", L"bEnabled", d.heal);
			v.Float(L"Heal", L"fCooldown", t.healCooldown, 10.0f, 600.0f);
			v.Float(L"Heal", L"fAmountBase", t.healAmountBase, 0.0f, 2000.0f);
			v.Float(L"Heal", L"fAmountPerLevel", t.healAmountPerLevel, 0.0f, 50.0f);
			v.Float(L"Heal", L"fAmountMax", t.healAmountMax, 0.0f, 5000.0f);
			v.Float(L"Heal", L"fReach", t.healReach, 500.0f, 10000.0f);
			v.Bool(L"Heal", L"bAnySpell", d.healersAnySpell);

			v.Bool(L"Rejuvenate", L"bEnabled", d.rejuvenate);
			v.Float(L"Rejuvenate", L"fSeconds", t.rejuvenateSeconds, 5.0f, 600.0f);
			v.Float(L"Rejuvenate", L"fSecondsLevel30", t.rejuvenateSecondsAt30, 5.0f, 600.0f);
			v.Float(L"Rejuvenate", L"fSecondsLevel60", t.rejuvenateSecondsAt60, 5.0f, 900.0f);
			v.Float(L"Rejuvenate", L"fRegenBonus", t.rejuvenateBonus, 10.0f, 500.0f);

			v.Float(L"Targeting", L"fEngageRange", t.engageRange, 500.0f, 10000.0f);
			v.Float(L"Targeting", L"fGuardRadius", t.guardRadius, 100.0f, 5000.0f);
			v.Float(L"Targeting", L"fHoldBackRadius", t.holdBackRadius, 100.0f, 5000.0f);
			v.Float(L"Targeting", L"fWithMeRadius", t.withMeRadius, 150.0f, 1500.0f);
			v.Float(L"Targeting", L"fDefendRange", t.defendRange, 100.0f, 1500.0f);
			v.Float(L"Targeting", L"fStickiness", t.stickiness, 0.0f, 500.0f);
			v.Float(L"Targeting", L"fSpreadPenalty", t.spreadPenalty, 0.0f, 500.0f);
			v.Float(L"Targeting", L"fDistanceWeight", t.distanceWeight, 0.0f, 1.0f);
			v.Bool(L"Targeting", L"bMarkGlow", d.markGlow);
			v.String(L"Targeting", L"sMarkShader", d.markShader);
			v.Bool(L"Targeting", L"bLeash", d.guardLeash);
			v.Float(L"Targeting", L"fHoldBackLeash", d.holdBackLeash, 300.0f, 4000.0f);

			v.Bool(L"Specials", L"bEnabled", d.specials);

			v.Bool(L"Effects", L"bEnabled", d.abilityEffects);

			v.Float(L"PathBonus", L"fFighterAttackSpeed", t.bonusAttackSpeed, 0.0f, 10.0f);
			v.Float(L"PathBonus", L"fTankDamageTaken", t.bonusDamageTaken, 0.0f, 10.0f);
			v.Float(L"PathBonus", L"fHealerHealing", t.bonusHealing, 0.0f, 10.0f);
			v.Float(L"PathBonus", L"fBruiserDamage", t.bonusDamage, 0.0f, 10.0f);
			v.Float(L"PathBonus", L"fArcherArrowsKept", t.bonusArrowsKept, 0.0f, 10.0f);
			v.Float(L"PathBonus", L"fMageMagickaCost", t.bonusMagickaCost, 0.0f, 10.0f);

			v.String(L"TimeEffect", L"sIntro", d.imodIntro);
			v.String(L"TimeEffect", L"sHold", d.imodHold);

			v.Int(L"Interface", L"iTheme", d.uiTheme, 0, UITheme::kCount - 1);
			v.Bool(L"Interface", L"bNotices", d.notices);
			v.Bool(L"Interface", L"bSounds", d.sounds);
			v.Bool(L"Interface", L"bTooltips", d.tooltips);
			v.Bool(L"Interface", L"bPathsGuideSeen", d.pathsGuideSeen);

			v.Bool(L"Debug", L"bLogDecisions", d.logDecisions);
		}

		void Sanitize(Data& a_data)
		{
			a_data.uiTheme = UITheme::Normalize(a_data.uiTheme);
			auto& t = a_data.tuning;
			t.tauntRadiusMax = std::max(t.tauntRadiusMax, t.tauntRadiusBase);
			t.cooldownMin = std::min(t.cooldownMin, t.cooldownBase);
			// The first mark glow was Detect Life's haze, written into the INI by any settings
			// change; it is the outline now.
			if (_stricmp(a_data.markShader.c_str(), "Skyrim.esm|0x0DC209") == 0) {
				a_data.markShader = Data{}.markShader;
			}
			a_data.planKey = Normalized(a_data.planKey);
			for (auto& hotkey : a_data.orderKeys) {
				hotkey = Normalized(hotkey);
			}
			a_data.overheadKey = Normalized(a_data.overheadKey);
		}

		[[nodiscard]] Lines Written(Data a_data)
		{
			Lines out;
			Visit(out, a_data);
			return out;
		}

		// Keys where the two differ.
		[[nodiscard]] int Differences(const Data& a_one, const Data& a_other)
		{
			const auto one = Written(a_one);
			const auto other = Written(a_other);
			int        count = 0;
			for (std::size_t i = 0; i < one.lines.size(); ++i) {
				count += one.lines[i].value != other.lines[i].value ? 1 : 0;
			}
			return count;
		}

		// A new user file starts with a word on what it is; the profile calls add the rest.
		bool EnsureUserFile(const std::wstring& a_path)
		{
			std::error_code ec;
			if (std::filesystem::exists(a_path, ec)) {
				return true;
			}
			std::ofstream out(std::filesystem::path(a_path), std::ios::binary);
			out << "; Fight With Me: the settings you changed in game, read after FightWithMe.ini.\r\n"
				   "; Updates never replace this file. Delete a line, or the whole file, to go back\r\n"
				   "; to what FightWithMe.ini says.\r\n\r\n";
			return static_cast<bool>(out);
		}

		// The Tanks and Healers pages: with the paths in charge of those roles, FightWithMe.ini's.
		void ShippedRoles(Data& a_data, const Data& a_shipped)
		{
			a_data.taunt = a_shipped.taunt;
			a_data.tauntOpener = a_shipped.tauntOpener;
			a_data.shieldWall = a_shipped.shieldWall;
			a_data.tauntChance = a_shipped.tauntChance;
			a_data.tauntCheckSeconds = a_shipped.tauntCheckSeconds;
			a_data.brace = a_shipped.brace;
			a_data.glowSeconds = a_shipped.glowSeconds;
			a_data.heal = a_shipped.heal;
			a_data.healersAnySpell = a_shipped.healersAnySpell;
			a_data.rejuvenate = a_shipped.rejuvenate;
			auto&       t = a_data.tuning;
			const auto& s = a_shipped.tuning;
			t.tauntRadiusBase = s.tauntRadiusBase;
			t.tauntRadiusPerSkill = s.tauntRadiusPerSkill;
			t.tauntRadiusMax = s.tauntRadiusMax;
			t.tauntReachDivisor = s.tauntReachDivisor;
			t.tauntFalloffPerLevel = s.tauntFalloffPerLevel;
			t.tauntFalloffLevels = s.tauntFalloffLevels;
			t.bossLevelBonus = s.bossLevelBonus;
			t.cowedLevelGap = s.cowedLevelGap;
			t.cowedHoldMult = s.cowedHoldMult;
			t.holdSeconds = s.holdSeconds;
			t.holdSecondsAt30 = s.holdSecondsAt30;
			t.holdSecondsAt60 = s.holdSecondsAt60;
			t.cooldownBase = s.cooldownBase;
			t.cooldownPerLevel = s.cooldownPerLevel;
			t.cooldownMin = s.cooldownMin;
			t.tauntStamina = s.tauntStamina;
			t.holdBreakDistance = s.holdBreakDistance;
			t.tauntRangedReach = s.tauntRangedReach;
			t.tauntArmorBase = s.tauntArmorBase;
			t.tauntArmorPerLevel = s.tauntArmorPerLevel;
			t.tauntArmorMax = s.tauntArmorMax;
			t.healCooldown = s.healCooldown;
			t.healAmountBase = s.healAmountBase;
			t.healAmountPerLevel = s.healAmountPerLevel;
			t.healAmountMax = s.healAmountMax;
			t.healReach = s.healReach;
			t.rejuvenateSeconds = s.rejuvenateSeconds;
			t.rejuvenateSecondsAt30 = s.rejuvenateSecondsAt30;
			t.rejuvenateSecondsAt60 = s.rejuvenateSecondsAt60;
			t.rejuvenateBonus = s.rejuvenateBonus;
		}

		// a_data is the player's own; what is live is the same, unless the paths hold the roles.
		void Publish(Data a_data)
		{
			Sanitize(a_data);
			const std::scoped_lock guard(versionsLock);
			versions.push_back(std::make_unique<Data>(std::move(a_data)));
			const Data* own = versions.back().get();
			chosen.store(own, std::memory_order_release);
			if (rolesLocked.load()) {
				auto live = std::make_unique<Data>(*own);
				ShippedRoles(*live, *shipped.load(std::memory_order_acquire));
				versions.push_back(std::move(live));
			}
			current.store(versions.back().get(), std::memory_order_release);
		}
	}

	void Load()
	{
		const Reader    own{ IniPath() };
		const Reader    user{ UserIniPath() };
		std::error_code ec;
		const bool      haveOwn = std::filesystem::exists(own.path, ec);
		const bool      haveUser = std::filesystem::exists(user.path, ec);
		Data            d;
		Visit(own, d);
		Sanitize(d);
		{
			const std::scoped_lock guard(versionsLock);
			versions.push_back(std::make_unique<Data>(d));
			shipped.store(versions.back().get(), std::memory_order_release);
		}
		Visit(user, d);
		Publish(d);

		const auto& live = Get();
		const auto  mine = haveUser ? std::format(" and FightWithMe_user.ini ({} of your changes)", Differences(Defaults(), live)) : std::string{};
		Log::Info("Settings {}{}: plan key {}, time x{:.2f}{}, taunt {}, heal {}, style weight {:.2f}."sv,
			haveOwn ? "read from FightWithMe.ini"sv : "defaulted (FightWithMe.ini not found)"sv, mine,
			live.planKey.key, live.timeScale, live.stopTime ? " (stopped)"sv : ""sv, live.taunt, live.heal, live.styleWeight);
	}

	const Data& Get() noexcept
	{
		return *current.load(std::memory_order_acquire);
	}

	const Data& Chosen() noexcept
	{
		return *chosen.load(std::memory_order_acquire);
	}

	void LockRoles(bool a_locked)
	{
		if (rolesLocked.exchange(a_locked) == a_locked) {
			return;
		}
		Publish(Chosen());
		Data mine = Chosen();
		ShippedRoles(mine, Defaults());
		const int kept = Differences(mine, Chosen());
		Log::Info("[Settings] {}"sv, a_locked ? std::format("Paths is installed: the Tanks and Healers settings follow FightWithMe.ini{}.",
													kept ? std::format("; your {} change(s) there are kept for when it is removed", kept) : std::string()) :
												  std::string("The Tanks and Healers settings are yours again."));
	}

	bool RolesLocked() noexcept
	{
		return rolesLocked.load();
	}

	Data Defaults()
	{
		return *shipped.load(std::memory_order_acquire);
	}

	bool Apply(const Data& a_data)
	{
		Publish(a_data);
		const auto   want = Written(Chosen());
		const auto   base = Written(Defaults());
		const auto   path = UserIniPath();
		const Reader user{ path };
		bool         ok = true;
		int          mine = 0, writes = 0;
		DWORD        error = 0;
		// One key at a time, in place: comments survive, and nothing is renamed or moved,
		// which MO2's virtual file system handles badly. A key back at FightWithMe.ini's
		// value leaves the user file.
		for (std::size_t i = 0; i < want.lines.size(); ++i) {
			const auto& line = want.lines[i];
			const bool  changed = line.value != base.lines[i].value;
			const auto  saved = user.Saved(line.section, line.key);
			mine += changed ? 1 : 0;
			if (changed ? saved == line.value : !saved) {
				continue;  // already so
			}
			if (changed && !EnsureUserFile(path)) {
				ok = false;
				error = ::GetLastError();
				break;
			}
			if (!::WritePrivateProfileStringW(line.section, line.key, changed ? line.value.c_str() : nullptr, path.c_str())) {
				ok = false;
				error = ::GetLastError();
			}
			++writes;
		}
		if (!ok) {
			Log::Error("[Settings] Could not write FightWithMe_user.ini (error {})."sv, error);
		} else if (writes > 0) {
			Log::Info("[Settings] Saved: {} setting(s) of yours in FightWithMe_user.ini."sv, mine);
		}
		return ok;
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

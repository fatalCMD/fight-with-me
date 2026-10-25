#include "FWM/Notices.h"

#include "FWM/Settings.h"
#include "FWM/Paths.h"
#include "FWM/Text.h"

namespace FWM::Notices
{
	namespace
	{
		std::mutex         lock;
		std::deque<Notice> notices;
	}

	void Push(std::string_view a_text, Kind a_kind, std::uint32_t a_icon, std::string_view a_title)
	{
		if (!Settings::Get().notices) {
			return;
		}
		const std::scoped_lock guard(lock);
		notices.push_back({ std::string(a_text), a_kind, ::GetTickCount64(), a_icon, std::string(a_title) });
		while (notices.size() > 6) {
			notices.pop_front();
		}
	}

	void Used(std::string_view a_text, Paths::Ability a_ability, Kind a_kind)
	{
		if (a_ability == Paths::Ability::kNone || static_cast<std::size_t>(a_ability) >= Paths::kAbilityCount) {
			Push(a_text, a_kind);
			return;
		}
		const auto& info = Paths::Info(a_ability);
		Push(a_text, a_kind, info.glyph, Tr(info.name));
	}

	std::vector<Notice> Recent(std::uint64_t a_maxAgeMs)
	{
		const auto             now = ::GetTickCount64();
		const std::scoped_lock guard(lock);
		while (!notices.empty() && now - notices.front().at > a_maxAgeMs) {
			notices.pop_front();
		}
		return { notices.begin(), notices.end() };
	}
}

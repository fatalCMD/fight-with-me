#include "FWM/Notices.h"

#include "FWM/Settings.h"

namespace FWM::Notices
{
	namespace
	{
		std::mutex         lock;
		std::deque<Notice> notices;
	}

	void Push(std::string a_text, Kind a_kind)
	{
		if (!Settings::Get().notices) {
			return;
		}
		const std::scoped_lock guard(lock);
		notices.push_back({ std::move(a_text), a_kind, ::GetTickCount64() });
		while (notices.size() > 6) {
			notices.pop_front();
		}
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

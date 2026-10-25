#include "FWM/Host.h"
#include <atomic>

namespace FWM::Host
{
	namespace
	{
		std::atomic<bool> pathsAlone{ false };
	}

	void SetPathsAlone()
	{
		pathsAlone = true;
	}

	bool PathsAlone() noexcept
	{
		return pathsAlone.load(std::memory_order_relaxed);
	}
}

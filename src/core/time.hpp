#pragma once

#include <chrono>

namespace adsnooze {

using MonotonicClock = std::chrono::steady_clock;
using MonotonicTimePoint = MonotonicClock::time_point;
using WallClock = std::chrono::system_clock;
using WallTimePoint = WallClock::time_point;

} // namespace adsnooze

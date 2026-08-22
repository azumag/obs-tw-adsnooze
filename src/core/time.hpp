#pragma once

#include <chrono>

namespace adsnooze {

using MonotonicClock = std::chrono::steady_clock;
using MonotonicTimePoint = MonotonicClock::time_point;
using WallClock = std::chrono::system_clock;
using WallTimePoint = std::chrono::time_point<WallClock, std::chrono::nanoseconds>;

} // namespace adsnooze

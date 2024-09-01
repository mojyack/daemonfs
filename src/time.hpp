#include <chrono>

using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

auto to_timespec(const TimePoint& time) -> timespec;

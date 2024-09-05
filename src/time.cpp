#include "time.hpp"

auto to_timespec(const TimePoint& time) -> timespec {
    const auto sec  = std::chrono::time_point_cast<std::chrono::seconds>(time);
    const auto nsec = std::chrono::time_point_cast<std::chrono::nanoseconds>(time);
    const auto diff = nsec - sec;

    auto spec    = timespec();
    spec.tv_sec  = sec.time_since_epoch().count();
    spec.tv_nsec = diff.count();
    return spec;
}

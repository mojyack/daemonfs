#pragma once
#include "macros/assert.hpp"

#define ensure_e(cond, ret, ...)                  \
    {                                             \
        constexpr auto error_value = ret;         \
        ensure_v(cond __VA_OPT__(, __VA_ARGS__)); \
    }


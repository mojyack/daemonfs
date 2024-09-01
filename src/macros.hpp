#pragma once
#include "util/assert.hpp"

#define ensure_e(cond, ret, ...)                            \
    if(!(cond)) {                                           \
        warn(__FILE__, ":", __LINE__, " assertion failed"); \
        return ret;                                         \
    }


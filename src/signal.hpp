#pragma once
#include <signal.h>

namespace sig {
using SignalHandler = void(int signal);

auto empty_siget() -> sigset_t;
auto block(int signal, bool block) -> bool;
auto set_handler(int signal, SignalHandler handler) -> bool;
} // namespace sig

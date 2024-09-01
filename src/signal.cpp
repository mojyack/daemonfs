#include <signal.h>

#include "macros/assert.hpp"
#include "signal.hpp"

namespace sig {
auto empty_siget() -> sigset_t {
    auto set = sigset_t();
    sigemptyset(&set);
    return set;
}

auto block(const int signal, const bool block) -> bool {
    auto set = empty_siget();
    ensure(sigaddset(&set, signal) == 0);
    ensure(sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL) == 0);
    return true;
}

auto set_handler(const int signal, SignalHandler handler) -> bool {
    using SigAction = struct sigaction;

    auto act       = SigAction();
    act.sa_handler = handler;
    act.sa_flags   = 0;
    ensure(sigemptyset(&act.sa_mask) == 0);
    ensure(sigaction(signal, &act, NULL) == 0);
    return true;
}
} // namespace sig


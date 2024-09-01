#include <array>

#include <bits/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "macros/assert.hpp"
#include "signal.hpp"

constexpr auto etc = "/etc/init";

namespace {
auto siguser1_count = std::atomic_int(0);
auto siguser2_count = std::atomic_int(0);

auto siguser1_handler(int) -> void {
    siguser1_count.fetch_add(1);
}

auto siguser2_handler(int) -> void {
    siguser2_count.fetch_add(1);
}

auto ignore_handler(int) -> void {
}

auto child_main(const int stage, const char* const* envp) -> bool {
    const auto exec = build_string(etc, "/", stage);
    const auto argv = std::array{exec.data(), (const char*)nullptr};
    ensure(setsid() != pid_t(-1));
    ensure(chdir(etc) != -1);
    ensure(execve(exec.data(), (char**)argv.data(), (char**)envp) != -1);
    bail("unable to start child of stage ", stage, ": ", strerror(errno));
}

auto run(const char* const* envp) -> int {
    ensure(getpid() == 1, "must be run as process no 1.");
    ensure(setsid() != pid_t(-1));

    ensure(sig::set_handler(SIGUSR1, siguser1_handler));
    ensure(sig::set_handler(SIGUSR2, siguser2_handler));
    ensure(sig::set_handler(SIGINT, ignore_handler));
    ensure(sig::set_handler(SIGTERM, ignore_handler));
    ensure(sig::set_handler(SIGPIPE, ignore_handler));

    if(const auto ttyfd = open("/dev/console", O_RDWR); ttyfd != -1) {
        ensure(dup2(ttyfd, 0) != -1);
        ensure(dup2(ttyfd, 1) != -1);
        ensure(dup2(ttyfd, 2) != -1);
        if(ttyfd > 2) {
            close(ttyfd);
        }
    }

    ensure(reboot(RB_DISABLE_CAD) == 0);

    auto do_reboot = false;
    for(auto stage = 1; stage <= 3; stage += 1) {
        const auto pid = fork();
        ensure(pid != -1);
        if(pid == 0) {
            ensure(child_main(stage, envp));
            bail("unreachable");
        }
        while(true) {
            if(siguser1_count.exchange(0)) {
                do_reboot = false;
            }
            if(siguser2_count.exchange(0)) {
                do_reboot = true;
            }

            auto       status = 0;
            const auto child  = waitpid(-1, &status, 0);
            if(child == -1) {
                if(errno != EINTR) {
                    warn("waitpid() failed: ", strerror(errno));
                    sleep(5);
                }
                continue;
            }
            if(child == pid) {
                // goto next stage
                break;
            }
        }
    }

    print("sending KILL signal to all processes...");
    kill(-1, SIGKILL);

    sync();
    ensure(reboot(do_reboot ? RB_AUTOBOOT : RB_POWER_OFF) != -1);
    bail("unreachable");
    return 0;
}
} // namespace

auto main(const int /*argc*/, const char* const* /*argv*/, const char* const* envp) -> int {
    if(run(envp) != 0) {
        warn("init exitted unexpectedlly");
        warn("fallback to emergency shell");

        const auto argv = std::array{"/sbin/agetty", "--noclear", "tty1", "linux", (const char*)nullptr};
        execve(argv[0], (char**)argv.data(), (char**)envp);
    }

    return 0;
}

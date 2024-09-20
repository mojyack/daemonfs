#include <bits/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "daemonfs.hpp"
#include "macros.hpp"
#include "macros/unwrap.hpp"
#include "signal.hpp"
#include "util/split.hpp"

namespace {
const auto uid = getuid();
const auto gid = getgid();

auto dir_attr(Stat& stat) -> void {
    stat.st_mode  = S_IFDIR | 0755;
    stat.st_nlink = 1;
    stat.st_uid   = uid;
    stat.st_gid   = gid;
}

auto extract_string(const std::string& data) -> std::string_view {
    auto str = std::string_view(data);
    if(str.empty()) {
        return str;
    }
    if(str.back() == '\n') {
        str.remove_suffix(1);
    }
    return str;
}

auto sigchild_count = std::atomic_int();

auto sigchild_handler(int) -> void {
    sigchild_count.fetch_add(1);
}
} // namespace

auto DaemonFS::find_daemon(const std::string_view name) -> Daemon* {
    auto daemon_it = std::ranges::find_if(daemons, [name](auto& d) { return d->name == name; });
    if(daemon_it == daemons.end()) {
        return nullptr;
    }
    return daemon_it->get();
}

auto DaemonFS::find_daemon_and_filename(std::string_view path) -> std::pair<Daemon*, std::string_view> {
    const auto elms = split(path, "/");
    if(elms.size() != 2) {
        return {nullptr, ""};
    }
    const auto name   = elms[0];
    const auto file   = elms[1];
    const auto daemon = find_daemon(name);
    return {daemon, file};
}

auto DaemonFS::start_daemon(Daemon& daemon) -> bool {
    ensure(daemon.start_process());
    daemon.set_state(State::Up);

    static_assert(alignof(Daemon) % 2 == 0);
    auto event = epoll_event{.events = EPOLLIN, .data = {.ptr = &daemon}};
    ensure(epoll_ctl(epollfd, EPOLL_CTL_ADD, daemon.stdout_fd, &event) == 0, strerror(errno));
    event.data.ptr = (void*)(uintptr_t(&daemon) | 1);
    ensure(epoll_ctl(epollfd, EPOLL_CTL_ADD, daemon.stderr_fd, &event) == 0, strerror(errno));
    return true;
}

auto DaemonFS::wait_daemon_process() -> void {
    auto       status = int();
    const auto joined = waitpid(-1, &status, WNOHANG);
    if(joined == -1) {
        bail("waitpid() error: ", strerror(errno));
    } else if(joined == 0) {
        bail("no process available for wait");
    }
    auto daemon_it = std::ranges::find_if(daemons, [joined](auto& d) { return d->pid == joined; });
    ensure(daemon_it != daemons.end(), "pid ", joined, " is not known daemon");
    auto& daemon = *daemon_it->get();
    if(WIFEXITED(status)) {
        print("daemon ", daemon.name, " exitted with code = ", WEXITSTATUS(status));
    } else {
        print("daemon ", daemon.name, " terminated with signal = ", WTERMSIG(status));
    }

    ensure(remove_fd_from_epollfds(daemon.stdout_fd));
    ensure(remove_fd_from_epollfds(daemon.stderr_fd));

    if(daemon.oneshot || daemon.state == State::WantDown) {
        daemon.set_state(State::Down);
        return;
    }

    const auto elapsed = std::chrono::system_clock::now() - daemon.state_changed;
    const auto fail    = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 5;
    if(fail) {
        print("daemon ", daemon.name, " failed to launch");
        daemon.set_state(State::Fail);
    } else {
        print("restarting daemon ", daemon.name);
        ensure(start_daemon(daemon));
    }
}

auto DaemonFS::remove_fd_from_epollfds(int& fd) -> bool {
    if(fd != -1) {
        ensure(epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == 0, strerror(errno));
        close(fd);
        fd = -1;
    }
    return true;
}

auto DaemonFS::process_command(const Commands::GetAttr& args) -> int {
    const auto path = std::string_view(args.path);
    if(path == "/") {
        dir_attr(*args.stbuf);
        set_timestamp(*args.stbuf, created);
        return 0;
    }
    const auto elms = split(path, "/");
    ensure_e(elms.size() == 1 || elms.size() == 2, -EINVAL);
    const auto name   = elms[0];
    const auto daemon = find_daemon(name);
    if(!daemon) {
        // intentionally not a ensure_e
        return -ENOENT;
    }
    if(elms.size() == 1) {
        dir_attr(*args.stbuf);
        set_timestamp(*args.stbuf, daemon->created);
        return 0;
    }
    return daemon->getattr(elms[1], *args.stbuf);
}

auto DaemonFS::process_command(const Commands::MakeDir& args) -> int {
    const auto path = std::string_view(args.path);
    const auto elms = split(path, "/");
    ensure_e(elms.size() == 1, -EINVAL);
    const auto name   = elms[0];
    const auto daemon = find_daemon(name);
    ensure_e(!daemon, -EEXIST);
    daemons.emplace_back(new Daemon{.name = std::string(name)});
    return 0;
}

auto DaemonFS::process_command(const Commands::RemoveDir& args) -> int {
    const auto path = std::string_view(args.path);
    const auto elms = split(path, "/");
    ensure_e(elms.size() == 1, -EINVAL);
    const auto name      = elms[0];
    const auto daemon_it = std::ranges::find_if(daemons, [name](auto& d) { return d->name == name; });
    ensure_e(daemon_it != daemons.end(), -ENOENT);
    const auto daemon = *(*daemon_it);
    ensure_e(daemon.state != State::Up && daemon.state != State::WantDown, -EBUSY);
    daemons.erase(daemon_it);
    return 0;
}

auto DaemonFS::process_command(const Commands::ReadDir& args) -> int {
    const auto path = std::string_view(args.path);
    if(path == "/") {
        for(const auto& daemon : daemons) {
            args.filler(args.buf, daemon->name.data(), NULL, 0, fuse_fill_dir_flags(0));
        }
        return 0;
    }
    const auto elms = split(path, "/");
    ensure_e(elms.size() == 1, -EINVAL);
    const auto name   = elms[0];
    const auto daemon = find_daemon(name);
    ensure_e(daemon, -ENOENT);
    return daemon->readdir([&args](const char* const name, const Stat& stat) {
        return args.filler(args.buf, name, &stat, 0, fuse_fill_dir_flags(0)) == 0;
    });
}

auto DaemonFS::process_command(const Commands::Truncate& args) -> int {
    const auto [daemon, file] = find_daemon_and_filename(args.path);
    ensure(daemon, -ENOENT);
    return daemon->truncate(file, args.offset);
}

auto DaemonFS::process_command(const Commands::Read& args) -> int {
    const auto [daemon, file] = find_daemon_and_filename(args.path);
    ensure(daemon, -ENOENT);
    return daemon->read(file, *args.buf);
}

auto DaemonFS::process_command(const Commands::Write& args) -> int {
    const auto [daemon, file] = find_daemon_and_filename(args.path);
    ensure(daemon, -ENOENT);

    if(file == "state") {
        const auto str = extract_string(*args.buf);
        if(str == "up") {
            ensure_e(daemon->state == State::Down || daemon->state == State::Fail, -EINVAL);
            ensure_e(start_daemon(*daemon), -EIO);
        } else if(str == "down") {
            ensure_e(daemon->state == State::Up, -EINVAL);
            daemon->set_state(State::WantDown);
            ensure_e(kill(daemon->pid, SIGTERM) == 0, -EIO);
        } else {
            return -EINVAL;
        }
        return 0;
    }

    return daemon->write(file, *args.buf);
}

auto DaemonFS::process_command(const Commands::Quit& /*args*/) -> int {
    running = false;
    return 0;
}

auto DaemonFS::process_requests() -> void {
    for(auto& request : requests.swap()) {
        unwrap(result, request.command.apply([this](auto& command) -> int {
            return process_command(command);
        }));
        request.notify->result = result;
        request.notify->event.notify();
    }
}

auto DaemonFS::init() -> bool {
    requests_event = eventfd(0, EFD_CLOEXEC);
    ensure(requests_event >= 0, strerror(errno));

    epollfd = epoll_create1(EPOLL_CLOEXEC);
    ensure(epollfd >= 0, strerror(errno));
    auto event = epoll_event{.events = EPOLLIN, .data = {.ptr = &requests}};
    ensure(epoll_ctl(epollfd, EPOLL_CTL_ADD, requests_event, &event) == 0, strerror(errno));
    return true;
}

auto DaemonFS::run() -> bool {
    ensure(sig::block(SIGCHLD, true));
    ensure(sig::set_handler(SIGCHLD, sigchild_handler));

    running = true;

    auto event     = epoll_event();
    auto empty_set = sig::empty_siget();
loop:
    if(!running) {
        return true;
    }

    const auto poll = epoll_pwait(epollfd, &event, 1, -1, &empty_set);
    if(poll == -1 && errno != EINTR) {
        warn("epoll_pwait error: ", strerror(errno));
        goto loop;
    }
    if(poll == -1) {
        goto wait;
    }
    if(event.data.ptr == &requests) {
        if(event.events & EPOLLIN) {
            auto buf = uint64_t();
            read(requests_event, &buf, sizeof(buf));
            process_requests();
        }
    } else {
        const auto ptr       = (uintptr_t)event.data.ptr;
        auto&      daemon    = *std::bit_cast<Daemon*>(ptr & ~uintptr_t(1));
        const auto is_stderr = (ptr & 1) == 1;
        auto&      fd        = is_stderr ? daemon.stderr_fd : daemon.stdout_fd;
        if(event.events & EPOLLIN) {
            auto buf = std::array<char, 256>();
            while(true) {
                const auto len = read(fd, buf.data(), buf.size());
                if((len < 0 && errno == EAGAIN) || len == 0) {
                    break;
                }
                if(len < 0) {
                    line_warn("read() failed: ", strerror(errno));
                    break;
                }
                if(verbose) {
                    print(daemon.name, ": ", std::string_view{buf.data(), size_t(len)});
                }
                (is_stderr ? daemon.stderr_buf : daemon.stdout_buf).write({buf.data(), size_t(len)});
            }
        }
        if(event.events & EPOLLHUP) {
            // daemon closed other end of the pipe
            ensure(remove_fd_from_epollfds(fd));
        }
    }
wait:
    for(auto l = sigchild_count.exchange(0); l > 0; l -= 1) {
        wait_daemon_process();
    }
    goto loop;
}

auto DaemonFS::add_oneshot_daemon(std::string name, std::string path) -> bool {
    auto& daemon = daemons.emplace_back(new Daemon{
        .name    = std::move(name),
        .args    = {std::move(path)},
        .oneshot = true,
    });
    daemon->set_state(State::Down);
    ensure(start_daemon(*daemon));
    return true;
}

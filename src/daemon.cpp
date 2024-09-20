#include <chrono>
#include <filesystem>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "daemon.hpp"
#include "macros.hpp"
#include "util/split.hpp"

namespace {
const auto uid = getuid();
const auto gid = getgid();

const auto state_str = std::array{"init", "up", "want-down", "down", "fail"};

auto is_pid_valid(const State state) {
    return state == State::Up || state == State::WantDown;
}
} // namespace

auto set_timestamp(Stat& stat, const TimePoint& time) -> void {
    const auto ts = to_timespec(time);
    stat.st_ctim  = ts;
    stat.st_mtim  = ts;
    stat.st_atim  = ts;
}

auto Daemon::start_process() -> bool {
    auto pipe_stdout = std::array<int, 2>();
    auto pipe_stderr = std::array<int, 2>();
    ensure_e(pipe2(pipe_stdout.data(), O_NONBLOCK | O_CLOEXEC) >= 0, false);
    ensure_e(pipe2(pipe_stderr.data(), O_NONBLOCK | O_CLOEXEC) >= 0, false);
    pid = fork();
    if(pid == -1) {
        warn("fork() failed: ", strerror(errno));
        close(pipe_stdout[0]);
        close(pipe_stderr[0]);
        close(pipe_stdout[1]);
        close(pipe_stderr[1]);
        return false;
    }
    if(pid != 0) {
        // parent
        close(pipe_stdout[1]);
        close(pipe_stderr[1]);
        stdout_fd = pipe_stdout[0];
        stderr_fd = pipe_stderr[0];
        return true;
    }
    // child
    dup2(open("/dev/null", O_RDONLY | O_CLOEXEC), 0);
    dup2(pipe_stdout[1], 1);
    dup2(pipe_stderr[1], 2);

    const auto workdir = std::filesystem::path(args[0]).parent_path().string();
    if(chdir(workdir.data()) == -1) {
        warn("chdir() failed: ", strerror(errno));
        _exit(1);
    }

    auto argv = std::vector<char*>(args.size() + 1);
    for(auto i = 0u; i < args.size(); i += 1) {
        argv[i] = args[i].data();
    }
    argv.emplace_back(nullptr);
    execve(argv[0], argv.data(), environ);
    warn("execve() failed: ", strerror(errno));
    _exit(1);
}

auto Daemon::set_state(const State new_state) -> void {
    state         = new_state;
    state_changed = std::chrono::system_clock::now();
}

auto Daemon::getattr(const std::string_view file, Stat& stat) const -> int {
    stat.st_nlink = 1;
    stat.st_uid   = uid;
    stat.st_gid   = gid;
    stat.st_mode  = S_IFREG | 0644;
    set_timestamp(stat, created);
    if(file == "args") {
        return 0;
    }
    ensure_e(state != State::Init, -ENOENT);
    if(file == "state") {
        stat.st_mtim = to_timespec(state_changed);
        return 0;
    }
    if(file == "stdout") {
        stat.st_size = stdout_buf.data.size();
        return 0;
    }
    if(file == "stderr") {
        stat.st_size = stderr_buf.data.size();
        return 0;
    }
    stat.st_mode = S_IFREG | 0444;
    if(file == "pid" && is_pid_valid(state)) {
        return 0;
    }
    return -ENOENT;
}

auto Daemon::readdir(AddDirEntry callback) const -> int {
    auto stat    = Stat();
    stat.st_mode = S_IFREG;
    ensure_e(callback("args", stat), -EIO);
    if(state == State::Init) {
        return 0;
    }
    ensure_e(callback("state", stat), -EIO);
    if(is_pid_valid(state)) {
        ensure_e(callback("pid", stat), -EIO);
    }
    stat.st_size = 4096;
    ensure_e(callback("stdout", stat), -EIO);
    ensure_e(callback("stderr", stat), -EIO);
    return 0;
}

auto Daemon::truncate(const std::string_view file, const off_t offset) -> int {
    if(file == "stdout") {
        stdout_buf.resize(offset);
    } else if(file == "stderr") {
        stderr_buf.resize(offset);
    } else {
        return -EINVAL;
    }
    return 0;
}

auto Daemon::read(std::string_view file, std::string& buf) const -> int {
    if(file == "args") {
        for(const auto& arg : args) {
            buf += arg;
            buf += '\n';
        }
        return 0;
    }
    ensure_e(state != State::Init, -EINVAL);
    if(file == "state") {
        buf = state_str[int(state)];
        return 0;
    }
    if(file == "pid") {
        ensure_e(is_pid_valid(state), -EINVAL);
        buf = std::to_string(pid);
        return 0;
    }
    if(file == "stdout") {
        buf.resize(stdout_buf.len);
        stdout_buf.read(buf);
        return 0;
    }
    if(file == "stderr") {
        buf.resize(stderr_buf.len);
        stderr_buf.read(buf);
        return 0;
    }
    return -ENOENT;
}

auto Daemon::write(std::string_view file, const std::string& buf) -> int {
    if(file == "args") {
        ensure_e(state == State::Init, -EINVAL);
        ensure_e(!buf.empty(), -EINVAL);
        const auto elms = split(buf, "\n");
        ensure_e(std::filesystem::path(elms[0]).is_absolute(), -EINVAL);
        for(const auto arg : elms) {
            args.emplace_back(std::string(arg));
        }
        set_state(State::Down);
        return 0;
    }
    return -ENOENT;
}

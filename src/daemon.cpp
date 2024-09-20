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

// modifies args
auto split_to_argv(std::string& args) -> std::vector<char*> {
    auto argv = std::vector<char*>();
    args.push_back('\n');
    for(const auto arg : split(args, "\n")) {
        const auto ptr  = const_cast<char*>(arg.data());
        ptr[arg.size()] = '\0'; // \n to \0
        argv.emplace_back(ptr);
    }
    argv.emplace_back(nullptr);

    return argv;
}

auto memcpy_range(std::string_view file, const size_t offset, const size_t size, const void* const buffer, const bool write) -> int {
    const auto copy_head = offset;
    const auto copy_end  = std::min(offset + size, file.size());
    const auto copy_len  = copy_end - copy_head;
    if(write) {
        std::memcpy((char*)file.data() + copy_head, buffer, copy_len);
    } else {
        std::memcpy((char*)buffer, file.data() + copy_head, copy_len);
    }
    return copy_len;
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

    const auto argv    = split_to_argv(args);
    const auto workdir = std::filesystem::path(argv[0]).parent_path().string();
    if(chdir(workdir.data()) == -1) {
        warn("chdir() failed: ", strerror(errno));
        _exit(1);
    }

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

auto Daemon::read(const std::string_view file, const size_t offset, const size_t size, char* const buffer) const -> int {
    if(file == "args") {
        return memcpy_range(args, offset, size, buffer, false);
    }
    ensure_e(state != State::Init, -EINVAL);
    if(file == "state") {
        return memcpy_range(state_str[int(state)], offset, size, buffer, false);
    }
    if(file == "pid") {
        ensure_e(is_pid_valid(state), -EINVAL);
        return memcpy_range(std::to_string(pid), offset, size, buffer, false);
    }
    if(file == "stdout") {
        return stdout_buf.read(offset, {buffer, size});
    }
    if(file == "stderr") {
        return stderr_buf.read(offset, {buffer, size});
    }
    return -ENOENT;
}

auto Daemon::write(const std::string_view file, const size_t offset, const size_t size, const char* const buffer) -> int {
    if(file == "args") {
        ensure_e(state == State::Init, -EINVAL);
        set_state(State::Down);
        args.resize(offset + size);
        return memcpy_range(args, offset, size, buffer, true);
    }
    return -ENOENT;
}

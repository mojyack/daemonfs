#pragma once
#include <chrono>
#include <functional>
#include <span>
#include <string>

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

#include "message-buffer.hpp"
#include "time.hpp"

using Stat        = struct stat;
using AddDirEntry = std::function<bool(const char* const name, const Stat& stat)>;

enum class State {
    Init = 0,
    Up,
    WantDown,
    Down,
    Fail,
};

enum class StopResult {
    Ok,
    Pending,
    Error,
};

auto set_timestamp(Stat& stat, const TimePoint& time) -> void;

struct Daemon {
    std::string              name;
    std::vector<std::string> args;
    State                    state : 7     = State::Init;
    bool                     oneshot       = false;
    TimePoint                created       = std::chrono::system_clock::now();
    TimePoint                state_changed = created;
    MessageBuffer            stdout_buf;
    MessageBuffer            stderr_buf;

    // child process state
    int   stdout_fd = -1;
    int   stderr_fd = -1;
    pid_t pid;

    auto start_process() -> bool;
    auto set_state(State new_state) -> void;

    auto getattr(std::string_view file, Stat& stat) const -> int;
    auto readdir(AddDirEntry callback) const -> int;
    auto truncate(std::string_view file, off_t offset) -> int;
    auto read(std::string_view file, std::string& buf) const -> int;
    auto write(std::string_view file, const std::string& buf) -> int;
};

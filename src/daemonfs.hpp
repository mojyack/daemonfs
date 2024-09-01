#pragma once
#include <sys/epoll.h>
#include <unistd.h>

#include "daemon.hpp"
#include "util/event.hpp"
#include "util/variant.hpp"
#include "util/writers-reader-buffer.hpp"

struct RemoteCommandNotify {
    Event event;
    int   result;
};

struct Commands {
    struct GetAttr {
        const char* path;
        Stat*       stbuf;
    };

    struct MakeDir {
        const char* path;
    };

    struct RemoveDir {
        const char* path;
    };

    struct ReadDir {
        const char*     path;
        void*           buf;
        fuse_fill_dir_t filler;
    };

    struct Truncate {
        const char* path;
        off_t       offset;
    };

    struct Read {
        const char*  path;
        std::string* buf;
    };

    struct Write {
        const char*        path;
        const std::string* buf;
    };

    struct Quit {
    };

    using Command = Variant<GetAttr, MakeDir, RemoveDir, ReadDir, Truncate, Read, Write, Quit>;
};

using Command = Commands::Command;

struct Request {
    RemoteCommandNotify* notify;
    Command              command;
};

class DaemonFS {
  private:
    constexpr static auto error_value = -EINVAL;

    TimePoint created = std::chrono::system_clock::now();

    int                                  epollfd;
    int                                  requests_event;
    WritersReaderBuffer<Request>         requests;
    std::vector<std::unique_ptr<Daemon>> daemons;
    bool                                 running;

    auto find_daemon(std::string_view name) -> Daemon*;
    auto find_daemon_and_filename(std::string_view path) -> std::pair<Daemon*, std::string_view>;
    auto start_daemon(Daemon& daemon) -> bool;
    auto wait_daemon_process() -> void;
    auto remove_fd_from_epollfds(int& fd) -> bool;

    auto process_command(const Commands::GetAttr& args) -> int;
    auto process_command(const Commands::MakeDir& args) -> int;
    auto process_command(const Commands::RemoveDir& args) -> int;
    auto process_command(const Commands::ReadDir& args) -> int;
    auto process_command(const Commands::Truncate& args) -> int;
    auto process_command(const Commands::Read& args) -> int;
    auto process_command(const Commands::Write& args) -> int;
    auto process_command(const Commands::Quit& args) -> int;
    auto process_requests() -> void;

  public:
    bool verbose = true;

    auto init() -> bool;
    auto run() -> bool;
    auto add_oneshot_daemon(std::string name, std::string path) -> bool;

    template <class T, class... Args>
    auto remote_command(const Args... args) -> int;
};

template <class T, class... Args>
auto DaemonFS::remote_command(const Args... args) -> int {
    if(verbose) {
        printf("new command %lu\n", Command::index_of<T>);
    }

    auto notify = RemoteCommandNotify();
    requests.push(Request{.notify = &notify, .command = Command::create<T>(args...)});
    auto buf = uint64_t(1);
    write(requests_event, &buf, sizeof(buf));
    notify.event.wait();
    if(verbose) {
        printf("done result = %d %s\n", notify.result, strerror(-notify.result));
    }
    return notify.result;
}

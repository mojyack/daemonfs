#include <filesystem>
#include <thread>

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "daemonfs.hpp"
#include "macros/assert.hpp"
#include "util/argument-parser.hpp"

namespace {
struct PageCache {
    std::string data;
};

auto fs = (DaemonFS*)(nullptr);

auto bootstrap_path = std::string();

auto getattr(const char* const path, Stat* const stbuf, fuse_file_info* /*fi*/) -> int {
    return fs->remote_command<Commands::GetAttr>(path, stbuf);
}

auto mkdir(const char* const path, const mode_t /*mode*/) -> int {
    return fs->remote_command<Commands::MakeDir>(path);
}

auto rmdir(const char* const path) -> int {
    return fs->remote_command<Commands::RemoveDir>(path);
}

auto readdir(const char* const path, void* const buf, const fuse_fill_dir_t filler, const off_t /*offset*/, fuse_file_info* const /*fi*/, const fuse_readdir_flags /*flags*/) -> int {
    return fs->remote_command<Commands::ReadDir>(path, buf, filler);
}

auto truncate(const char* const path, const off_t offset, fuse_file_info* /*fi*/) -> int {
    return fs->remote_command<Commands::Truncate>(path, offset);
}

auto open(const char* const path, fuse_file_info* const fi) -> int {
    fi->direct_io   = 1;
    fi->nonseekable = 1;
    fi->noflush     = 1;

    auto cache = std::unique_ptr<PageCache>(new PageCache());
    if(const auto code = fs->remote_command<Commands::Read>(path, &cache->data); code != 0) {
        return code;
    }
    fi->fh = std::bit_cast<uintptr_t>(cache.release());
    return 0;
}

auto read(const char* const /*path*/, char* const buf, const size_t size, const off_t offset, fuse_file_info* const fi) -> int {
    if(fi->fh == 0) {
        return -EIO;
    }

    const auto cache = std::bit_cast<PageCache*>(fi->fh);
    if(size_t(offset) >= cache->data.size()) {
        return 0;
    }

    const auto copy_head = offset;
    const auto copy_end  = std::min(offset + size, cache->data.size());
    const auto copy_len  = copy_end - copy_head;
    std::memcpy(buf, cache->data.data() + copy_head, copy_len);
    return copy_len;
}

auto write(const char* const /*path*/, const char* const buf, const size_t size, const off_t offset, fuse_file_info* const fi) -> int {
    if(fi->fh == 0) {
        return -EIO;
    }

    const auto cache = std::bit_cast<PageCache*>(fi->fh);

    const auto copy_head = offset;
    const auto copy_end  = offset + size;
    const auto copy_len  = copy_end - copy_head;
    cache->data.resize(copy_end);
    std::memcpy(cache->data.data() + copy_head, buf, copy_len);
    return copy_len;
}

auto init(fuse_conn_info* /*conn*/, fuse_config* /*cfg*/) -> void* {
    if(!bootstrap_path.empty()) {
        fs->add_oneshot_daemon("bootstrap", std::move(bootstrap_path));
    }
    return NULL;
}

auto release(const char* const path, fuse_file_info* const fi) -> int {
    if(fi->fh == 0) {
        return -EIO;
    }

    const auto cache = std::bit_cast<PageCache*>(fi->fh);
    if(fi->flags & (O_RDWR | O_WRONLY)) {
        fs->remote_command<Commands::Write>(path, &cache->data);
    }
    delete cache;

    return 0;
}

const auto operations = fuse_operations{
    .getattr         = getattr,
    .readlink        = NULL,
    .mknod           = NULL,
    .mkdir           = mkdir,
    .unlink          = NULL,
    .rmdir           = rmdir,
    .symlink         = NULL,
    .rename          = NULL,
    .link            = link,
    .chmod           = NULL,
    .chown           = NULL,
    .truncate        = truncate,
    .open            = open,
    .read            = read,
    .write           = write,
    .statfs          = NULL,
    .flush           = NULL,
    .release         = release,
    .fsync           = NULL,
    .setxattr        = NULL,
    .getxattr        = NULL,
    .listxattr       = NULL,
    .removexattr     = NULL,
    .opendir         = NULL,
    .readdir         = readdir,
    .releasedir      = NULL,
    .fsyncdir        = NULL,
    .init            = init,
    .destroy         = NULL,
    .access          = NULL,
    .create          = NULL,
    .lock            = NULL,
    .utimens         = NULL,
    .bmap            = NULL,
    .ioctl           = NULL,
    .poll            = NULL,
    .write_buf       = NULL,
    .read_buf        = NULL,
    .flock           = NULL,
    .fallocate       = NULL,
    .copy_file_range = NULL,
    .lseek           = NULL,
};
} // namespace

auto main(const int argc, char** argv) -> int {
    auto mountpoint = (const char*)(nullptr);
    auto bootstrap  = (const char*)(nullptr);
    auto verbose    = false;
    auto help       = false;
    {
        auto parser = args::Parser();
        parser.kwarg(&bootstrap, {"-b"}, {"EXE", "bootstrap script", args::State::Initialized});
        parser.kwarg(&verbose, {"-v", "--verbose"}, {.arg_desc = "enable verbose outputs", .state = args::State::Initialized});
        parser.kwarg(&help, {"-h", "--help"}, {.arg_desc = "print help message", .state = args::State::Initialized, .no_error_check = true});
        parser.arg(&mountpoint, {.arg_desc = "mountpoint"});
        if(!parser.parse(argc, argv) || help) {
            print("usage: daemonfs ", parser.get_help());
            return 0;
        }
    }
    bootstrap_path = bootstrap != nullptr ? std::filesystem::absolute(bootstrap).string() : std::string();

    fs          = new DaemonFS();
    fs->verbose = verbose;
    ensure(fs->init());
    auto worker = std::thread([]() { fs->run(); });

    const auto args = std::array{"daemonfs", "-f", "-o", "default_permissions", "-o", "max_idle_threads=1", mountpoint};
    const auto ret  = fuse_main(args.size(), (char**)args.data(), &operations, NULL);
    fs->remote_command<Commands::Quit>();

    worker.join();

    return ret;
}

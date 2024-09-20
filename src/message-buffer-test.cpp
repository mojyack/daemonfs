#include "macros/assert.hpp"
#include "message-buffer.hpp"

namespace {
auto debug_print(const MessageBuffer& mb) -> void {
    printf("size=%lu len=%lu\n", mb.data.size(), mb.len);
    if(mb.len >= mb.data.size()) {
        for(auto i = 0u; i < mb.data.size(); i += 1) {
            printf("%c", mb.data[i]);
        }
    } else {
        for(auto i = 0u; i < mb.len; i += 1) {
            printf("%c", mb.data[i]);
        }
        for(auto i = mb.len; i < mb.data.size(); i += 1) {
            printf(".");
        }
    }
    printf("\n");
}
} // namespace

auto main() -> int {
    constexpr auto size = 8;

    auto mb = MessageBuffer();

    mb.resize(size);

    auto dump = [&mb]() {
        debug_print(mb);
        for(auto i = 0; i < size; i += 1) {
            auto buf = std::array<char, size>();
            print("read: ", i, " ", std::string_view{buf.data(), mb.read(i, buf)});
        }
    };

    ensure(mb.write({"hello", 5}) == 5);
    dump();

    ensure(mb.write({"!", 1}) == 1);
    dump();

    ensure(mb.write({"world", 5}) == 5);
    dump();

    ensure(mb.write({"!", 1}) == 1);
    dump();

    ensure(mb.write({"string", 6}) == 6);
    dump();

    for(auto i = 'a'; i < 'a' + 16; i += 1) {
        ensure(mb.write({&i, 1}) == 1);
        debug_print(mb);
    }
    dump();

    ensure(mb.write({"hello,world!", 12}) == 12);
    dump();

    mb.resize(4);
    ensure(mb.write({"hello,world!", 12}) == 12);
    dump();

    mb.resize(0);
    dump();

    mb.resize(size);
    dump();

    return 0;
}

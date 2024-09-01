#include "macros/assert.hpp"
#include "message-buffer.hpp"

namespace {
auto debug_print(const MessageBuffer& mb) -> void {
    printf("size=%lu head=%lu len=%lu\n", mb.data.size(), mb.head, mb.len);
    for(auto i = 0u; i < mb.head; i += 1) {
        printf(" ");
    }
    printf(".%lu\n", mb.len);
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

    auto buf = std::array<char, size>();
    auto mb  = MessageBuffer();

    mb.resize(size);

    ensure(mb.write({"hello", 5}) == 5);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    ensure(mb.write({"!", 1}) == 1);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    ensure(mb.write({"world", 5}) == 5);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    ensure(mb.write({"!", 1}) == 1);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    ensure(mb.write({"string", 6}) == 6);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    for(auto i = 'a'; i < 'a' + 16; i += 1) {
        ensure(mb.write({&i, 1}) == 1);
        debug_print(mb);
    }
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    ensure(mb.write({"hello,world!", 12}) == size);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    mb.resize(4);
    ensure(mb.write({"hello,world!", 12}) == 4);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    mb.resize(0);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    mb.resize(size);
    debug_print(mb);
    print("read: ", std::string_view{buf.data(), mb.read(buf)});

    return 0;
}

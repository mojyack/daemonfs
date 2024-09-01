#pragma once
#include <span>
#include <vector>

struct MessageBuffer {
    std::vector<char> data;
    size_t            head;
    size_t            len;

    auto resize(size_t size) -> void;
    auto read(std::span<char> buf) const -> size_t;
    auto write(std::span<const char> buf) -> size_t;
};


#include "message-buffer.hpp"

auto MessageBuffer::resize(const size_t size) -> void {
    auto new_data = std::vector<char>(size);

    head = 0;
    len  = read(new_data);
    data = std::move(new_data);
}

auto MessageBuffer::read(const std::span<char> buf) const -> size_t {
    auto left = std::min(len, buf.size());

    const auto copyable = data.size() - head;
    const auto copy_len = std::min(left, copyable);
    memcpy(buf.data(), data.data() + head, copy_len);
    left -= copy_len;
    memcpy(buf.data() + copy_len, data.data(), left);
    return std::min(len, buf.size());
}

auto MessageBuffer::write(std::span<const char> buf) -> size_t {
    if(buf.size() >= data.size()) {
        head = 0;
        len  = data.size();
        memcpy(data.data(), &buf.back() - data.size() + 1, len);
        return len;
    }

    const auto avail_head = (head + len) % data.size();
    const auto copyable   = data.size() - avail_head;
    if(copyable >= buf.size()) {
        memcpy(data.data() + avail_head, buf.data(), buf.size());

        const auto filled = len == data.size();
        if(filled) {
            head = (head + buf.size()) % data.size();
        } else {
            len += buf.size();
        }
        return buf.size();
    } else {
        memcpy(data.data() + avail_head, buf.data(), copyable);
        buf = buf.subspan(copyable);
        memcpy(data.data(), buf.data(), buf.size());

        head = buf.size();
        len  = data.size();
        return buf.size() + copyable;
    }
}

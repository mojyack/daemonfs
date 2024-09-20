#include "message-buffer.hpp"

auto MessageBuffer::resize(const size_t size) -> void {
    auto new_data = std::vector<char>(size);

    len  = read(0, new_data);
    data = std::move(new_data);
}

auto MessageBuffer::read(size_t offset, std::span<char> buf) const -> size_t {
    if(offset >= len || offset >= data.size()) {
        return 0;
    }

    const auto original_buf_size = buf.size();
    const auto sector_size       = data.size();

    if(len < data.size()) {
        const auto copy_len = std::min(len - offset, buf.size());
        memcpy(buf.data(), data.data() + offset, copy_len);
        return copy_len;
    }

    const auto end        = len % sector_size;
    const auto behind_end = sector_size - end;
    if(behind_end > offset) {
        const auto copy_len = std::min(behind_end - offset, buf.size());
        memcpy(buf.data(), data.data() + end + offset, copy_len);
        buf    = buf.subspan(copy_len);
        offset = 0;
    } else {
        offset -= behind_end;
    }
    const auto ahead_end = sector_size - behind_end;
    const auto copy_len  = std::min(ahead_end - offset, buf.size());
    memcpy(buf.data(), data.data() + offset, copy_len);
    return std::min(original_buf_size, sector_size - offset);
}

auto MessageBuffer::write(std::span<const char> buf) -> size_t {
    const auto original_buf_size = buf.size();
    const auto sector_size       = data.size();
    while(!buf.empty()) {
        const auto cursor     = len % sector_size;
        const auto free_space = sector_size - cursor;
        const auto copy_len   = std::min(buf.size(), free_space);
        memcpy(data.data() + cursor, buf.data(), copy_len);
        buf = buf.subspan(copy_len);
        len += copy_len;
    }

    return original_buf_size;
}

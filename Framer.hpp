#pragma once
#include <vector>
#include <cstdint>
#include <cstring>

class TerrariaFramer {
public:
    void feed(const uint8_t* data, size_t n, std::vector<std::vector<uint8_t>>& out) {
        buf_.insert(buf_.end(), data, data + n);
        while (buf_.size() >= 2) {
            uint16_t flen = uint16_t(buf_[0] | (buf_[1] << 8));
            if (flen < 3) {
                // skip one byte (bad length) — same as Python
                buf_.erase(buf_.begin());
                continue;
            }
            if (buf_.size() < flen) break;
            out.emplace_back(buf_.begin(), buf_.begin() + flen);
            buf_.erase(buf_.begin(), buf_.begin() + flen);
        }
    }

    void clear() { buf_.clear(); }

private:
    std::vector<uint8_t> buf_;
};

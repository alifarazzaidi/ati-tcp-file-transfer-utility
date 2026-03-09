#include "utils.h"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <stdexcept>
#include <unistd.h>

namespace tfu {

// Minimal SHA256 implementation (public domain, derived from pseudocode)
namespace {

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t choose(uint32_t e, uint32_t f, uint32_t g) {
    return (e & f) ^ (~e & g);
}

inline uint32_t majority(uint32_t a, uint32_t b, uint32_t c) {
    return (a & b) ^ (a & c) ^ (b & c);
}

inline uint32_t big_sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t big_sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t small_sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t small_sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

void sha256_transform(uint32_t state[8], const unsigned char block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + big_sigma1(e) + choose(e, f, g) + K[i] + w[i];
        uint32_t t2 = big_sigma0(a) + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

std::string to_hex(const unsigned char *data, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = data[i];
        out.push_back(hex_chars[(c >> 4) & 0xF]);
        out.push_back(hex_chars[c & 0xF]);
    }
    return out;
}

} // namespace

std::string sha256_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open file: " + path);
    }

    uint64_t total_len = 0;
    uint32_t state[8] = {
        0x6a09e667u,
        0xbb67ae85u,
        0x3c6ef372u,
        0xa54ff53au,
        0x510e527fu,
        0x9b05688cu,
        0x1f83d9abu,
        0x5be0cd19u,
    };

    std::array<unsigned char, 64> buffer{};
    size_t buffer_len = 0;

    while (in) {
        in.read(reinterpret_cast<char *>(buffer.data() + buffer_len), buffer.size() - buffer_len);
        std::streamsize read_n = in.gcount();
        if (read_n <= 0) break;

        total_len += static_cast<uint64_t>(read_n);
        buffer_len += static_cast<size_t>(read_n);

        while (buffer_len >= 64) {
            sha256_transform(state, buffer.data());
            if (buffer_len > 64) {
                size_t leftover = buffer_len - 64;
                std::rotate(buffer.begin(), buffer.begin() + 64, buffer.begin() + buffer_len);
                buffer_len = leftover;
            } else {
                buffer_len = 0;
            }
        }
    }

    // Padding
    buffer[buffer_len++] = 0x80;
    if (buffer_len > 56) {
        std::memset(buffer.data() + buffer_len, 0, 64 - buffer_len);
        sha256_transform(state, buffer.data());
        buffer_len = 0;
    }
    std::memset(buffer.data() + buffer_len, 0, 56 - buffer_len);
    buffer_len = 56;

    uint64_t bit_len = total_len * 8;
    for (int i = 7; i >= 0; --i) {
        buffer[buffer_len++] = static_cast<unsigned char>((bit_len >> (i * 8)) & 0xFF);
    }
    sha256_transform(state, buffer.data());

    unsigned char digest[32];
    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<unsigned char>((state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<unsigned char>((state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<unsigned char>((state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<unsigned char>(state[i] & 0xFF);
    }

    return to_hex(digest, sizeof(digest));
}

ssize_t read_exact(int fd, void *buf, size_t count) {
    unsigned char *ptr = static_cast<unsigned char *>(buf);
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t n = ::read(fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        ptr += n;
        remaining -= n;
    }
    return static_cast<ssize_t>(count - remaining);
}

ssize_t write_exact(int fd, const void *buf, size_t count) {
    const unsigned char *ptr = static_cast<const unsigned char *>(buf);
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t n = ::write(fd, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        ptr += n;
        remaining -= n;
    }
    return static_cast<ssize_t>(count - remaining);
}

uint64_t htonll(uint64_t x) {
    static const int check = 1;
    if (*(const char *)&check == 1) {
        uint32_t hi = htonl(static_cast<uint32_t>(x >> 32));
        uint32_t lo = htonl(static_cast<uint32_t>(x & 0xffffffffull));
        return (static_cast<uint64_t>(lo) << 32) | hi;
    }
    return x;
}

uint64_t ntohll(uint64_t x) {
    return htonll(x);
}

} // namespace tfu

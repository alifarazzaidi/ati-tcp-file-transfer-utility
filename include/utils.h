#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tfu {

// Compute SHA-256 hex string for file at given path.
// Throws std::runtime_error on error.
std::string sha256_file(const std::string &path);

// Network helpers
ssize_t read_exact(int fd, void *buf, size_t count);
ssize_t write_exact(int fd, const void *buf, size_t count);

// Convert 64-bit and 32-bit values to/from network byte order.
uint64_t htonll(uint64_t x);
uint64_t ntohll(uint64_t x);

} // namespace tfu

// Review candidate: shared limits for untrusted GRIB messages (C++11).
#ifndef GRIB_DECODE_LIMITS_H
#define GRIB_DECODE_LIMITS_H

#include <cstddef>
#include <cstdint>
#include <climits>
#include <cassert>
#include <limits>

// Explicit resource policy, overridable by the embedding application. These
// defaults permit a 128 MiB encoded message and a 128 MiB double-valued field.
// A constant packed field can otherwise request gigabytes from a few bytes.
#ifndef GRIB_MAX_MESSAGE_BYTES
#define GRIB_MAX_MESSAGE_BYTES (128u * 1024u * 1024u)
#endif
#ifndef GRIB_MAX_GRID_POINTS
#define GRIB_MAX_GRID_POINTS (16u * 1024u * 1024u)
#endif

#ifndef GRIB_MAX_READER_BYTES
#define GRIB_MAX_READER_BYTES (512u * 1024u * 1024u)
#endif

#ifndef GRIB_MAX_HEADER_SCAN_BYTES
#define GRIB_MAX_HEADER_SCAN_BYTES (1024u * 1024u)
#endif

namespace grib_decode {
inline std::uint32_t u32(const unsigned char* b) {
  return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) |
         (std::uint32_t(b[2]) << 8) | b[3];
}
inline bool gridSize(int nx, int ny, std::size_t& count) {
  if (nx <= 1 || ny <= 1) return false;
  const std::uint64_t n = std::uint64_t(nx) * std::uint64_t(ny);
  if (n > GRIB_MAX_GRID_POINTS || n > INT_MAX ||
      n > std::numeric_limits<std::size_t>::max() / sizeof(double)) return false;
  count = static_cast<std::size_t>(n);
  return true;
}
// Caller validates the complete bit block once. Four padding bytes at the end
// of the input allocation permit fast fixed-width loads, including the fifth
// byte needed by an unaligned 32-bit value. No checks inside the value loop.
inline std::uint32_t bits(const unsigned char* b, std::size_t first,
                          unsigned width) {
  assert(width <= 32);  // established at each section/template boundary
  if (!width) return 0;
  const std::size_t oct = first / 8;
  const unsigned shift = first % 8;
  std::uint32_t value = u32(b + oct);
  value <<= shift;
  if (width + shift > 32) value |= b[oct + 4] >> (8 - shift);
  return value >> (32 - width);
}
class BitCursor {
 public:
  BitCursor(const unsigned char* bytes, std::size_t length)
      : bytes_(bytes), end_(length * 8), offset_(0) {}
  bool has(std::uint64_t count) const { return count <= end_ - offset_; }
  std::uint32_t take(unsigned width) {
    const std::uint32_t value = bits(bytes_, offset_, width);
    offset_ += width;
    return value;
  }
  bool align() {
    const unsigned padding = (8 - offset_ % 8) % 8;
    if (!has(padding)) return false;
    offset_ += padding;
    return true;
  }
 private:
  const unsigned char* bytes_;
  std::size_t end_, offset_;
};
}
#endif

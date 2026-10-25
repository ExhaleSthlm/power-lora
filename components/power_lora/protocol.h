#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace esphome::power_lora {

// v1: four uint32 energy counters, then twenty-two uint16 measurements.
constexpr size_t FIELD_COUNT = 26;
constexpr size_t FRAME_SIZE = 76;
constexpr uint8_t VERSION = 1;
constexpr uint8_t FLAG_MOCK = 1;
constexpr uint32_t MISSING = UINT32_MAX;

struct Reading {
  uint16_t node{1};
  uint32_t boot{0};
  uint32_t sequence{0};
  uint8_t flags{FLAG_MOCK};
  std::array<uint32_t, FIELD_COUNT> values{};
};

inline uint16_t crc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;  // CRC-16/CCITT-FALSE
  for (size_t i = 0; i < length; i++) {
    crc ^= uint16_t(data[i]) << 8;
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}

inline void put(uint8_t *out, uint32_t value, size_t bytes) {
  for (size_t i = 0; i < bytes; i++) out[i] = uint8_t(value >> (8 * i));
}
inline uint32_t get(const uint8_t *in, size_t bytes) {
  uint32_t value = 0;
  for (size_t i = 0; i < bytes; i++) value |= uint32_t(in[i]) << (8 * i);
  return value;
}

inline std::array<uint8_t, FRAME_SIZE> encode(const Reading &reading) {
  std::array<uint8_t, FRAME_SIZE> out{};
  out[0] = 0x50; out[1] = 0x4C; out[2] = VERSION; out[3] = reading.flags;
  put(out.data() + 4, reading.node, 2);
  put(out.data() + 6, reading.boot, 4);
  put(out.data() + 10, reading.sequence, 4);
  size_t offset = 14;
  for (size_t i = 0; i < FIELD_COUNT; i++) {
    size_t width = i < 4 ? 4 : 2;
    // Reserve all-ones for missing/overflow; never silently wrap or clamp.
    uint32_t value = reading.values[i];
    if (width == 2 && value >= UINT16_MAX) value = UINT16_MAX;
    put(out.data() + offset, value, width);
    offset += width;
  }
  put(out.data() + FRAME_SIZE - 2, crc16(out.data(), FRAME_SIZE - 2), 2);
  return out;
}

inline bool decode(const uint8_t *data, size_t length, Reading &reading) {
  if (length != FRAME_SIZE || data[0] != 0x50 || data[1] != 0x4C ||
      data[2] != VERSION || (data[3] & ~FLAG_MOCK) != 0 ||
      get(data + FRAME_SIZE - 2, 2) != crc16(data, FRAME_SIZE - 2)) return false;
  reading.flags = data[3]; reading.node = get(data + 4, 2);
  reading.boot = get(data + 6, 4); reading.sequence = get(data + 10, 4);
  size_t offset = 14;
  for (size_t i = 0; i < FIELD_COUNT; i++) {
    size_t width = i < 4 ? 4 : 2;
    uint32_t value = get(data + offset, width);
    reading.values[i] = (width == 2 && value == UINT16_MAX) ? MISSING : value;
    offset += width;
  }
  return true;
}

// The receiver enables E220's one-byte RSSI suffix. The sender sends one
// complete frame per 128-byte sub-packet. Bound memory and resynchronise after
// corrupt bytes, a missing suffix, or a partial frame.
class StreamParser {
 public:
  bool feed(uint8_t byte, Reading &reading, int &rssi) {
    buffer_[used_++] = byte;
    while (used_ >= 2 && (buffer_[0] != 0x50 || buffer_[1] != 0x4C)) discard_();
    if (used_ < FRAME_SIZE + 1) return false;
    if (decode(buffer_.data(), FRAME_SIZE, reading)) {
      rssi = int(buffer_[FRAME_SIZE]) - 256;
      used_ = 0;
      return true;
    }
    errors++;
    discard_();
    while (used_ >= 2 && (buffer_[0] != 0x50 || buffer_[1] != 0x4C)) discard_();
    return false;
  }
  void reset() { used_ = 0; }
  uint32_t errors{0};
 private:
  void discard_() {
    std::move(buffer_.begin() + 1, buffer_.begin() + used_, buffer_.begin());
    used_--;
  }
  std::array<uint8_t, FRAME_SIZE + 1> buffer_{};
  size_t used_{0};
};

enum class SequenceResult { FIRST, NEW_BOOT, NEXT, GAP, DUPLICATE, OLD };
class SequenceTracker {
 public:
  SequenceResult accept(uint32_t boot, uint32_t sequence) {
    if (!seen_) {
      seen_ = true; boot_ = boot; last_ = sequence; return SequenceResult::FIRST;
    }
    if (boot != boot_) {
      boot_ = boot; last_ = sequence; return SequenceResult::NEW_BOOT;
    }
    uint32_t delta = sequence - last_;
    if (delta == 0) return SequenceResult::DUPLICATE;
    if (delta >= 0x80000000UL) return SequenceResult::OLD;
    last_ = sequence;
    missing += delta - 1;
    return delta == 1 ? SequenceResult::NEXT : SequenceResult::GAP;
  }
  uint32_t missing{0};
 private:
  bool seen_{false};
  uint32_t boot_{0}, last_{0};
};
}  // namespace esphome::power_lora

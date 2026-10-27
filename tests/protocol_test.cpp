#include "components/power_lora/protocol.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace esphome::power_lora;

static Reading sample() {
  Reading r;
  r.node = 0x1234; r.boot = 0x12345678; r.sequence = 0xABCDEF01;
  for (size_t i = 0; i < FIELD_COUNT; i++) r.values[i] = i < 2 ? 123456789U + i : 200U + i;
  return r;
}

int main() {
  const uint8_t check[]{'1','2','3','4','5','6','7','8','9'};
  assert(crc16(check, sizeof(check)) == 0x29B1);  // Independent known CRC vector.
  Reading input = sample(), output;
  auto frame = encode(input);
  assert(frame.size() == 52 && frame[4] == 0x34 && frame[5] == 0x12);
  assert(frame[6] == 0x78 && frame[9] == 0x12);
  assert(frame[14] == 0x15 && frame[15] == 0xCD && frame[16] == 0x5B && frame[17] == 0x07);
  assert(frame[2] == 2 && get(frame.data() + 18, 4) == input.values[1]);
  assert(get(frame.data() + 22, 2) == input.values[2]);
  assert(get(frame.data() + 48, 2) == input.values[15]);
  assert(decode(frame.data(), frame.size(), output));
  assert(output.values == input.values && output.boot == input.boot && output.sequence == input.sequence);
  for (size_t length = 0; length < frame.size(); length++) assert(!decode(frame.data(), length, output));
  // Every single-bit error, including header and checksum, must be rejected.
  for (size_t i = 0; i < frame.size(); i++) {
    for (int bit = 0; bit < 8; bit++) {
      auto corrupt = frame; corrupt[i] ^= 1 << bit;
      assert(!decode(corrupt.data(), corrupt.size(), output));
    }
  }
  input.values[0] = MISSING; input.values[4] = 65535; input.values[5] = 90000;
  frame = encode(input);
  assert(decode(frame.data(), frame.size(), output));
  assert(output.values[0] == MISSING && output.values[4] == MISSING && output.values[5] == MISSING);
  input = sample(); frame = encode(input);

  // Unsupported versions must be rejected even with a valid checksum.
  auto wrong_version = frame;
  wrong_version[2] = 1;
  put(wrong_version.data() + FRAME_SIZE - 2, crc16(wrong_version.data(), FRAME_SIZE - 2), 2);
  assert(!decode(wrong_version.data(), wrong_version.size(), output));
  // A real-length v1 frame must not publish data or prevent the following v2.
  std::array<uint8_t, 76> legacy{};
  legacy[0] = 0x50; legacy[1] = 0x4C; legacy[2] = 1; legacy[3] = FLAG_MOCK;
  put(legacy.data() + 74, crc16(legacy.data(), 74), 2);
  assert(!decode(legacy.data(), legacy.size(), output));
  StreamParser migration_parser;
  int migration_rssi = 0;
  for (auto byte : legacy) assert(!migration_parser.feed(byte, output, migration_rssi));
  assert(!migration_parser.feed(156, output, migration_rssi));
  for (auto byte : frame) assert(!migration_parser.feed(byte, output, migration_rssi));
  assert(migration_parser.feed(156, output, migration_rssi));
  assert(output.values == input.values);

  // For every truncation point, noise/partial data followed by a good packet
  // must recover, without publishing the incomplete packet.
  for (size_t cut = 0; cut < FRAME_SIZE; cut++) {
    StreamParser parser;
    int rssi = 0, accepted = 0;
    std::vector<uint8_t> bytes{0, 0x50, 0, 0x50};
    bytes.insert(bytes.end(), frame.begin(), frame.begin() + cut);
    bytes.insert(bytes.end(), frame.begin(), frame.end()); bytes.push_back(156);
    for (uint8_t byte : bytes) if (parser.feed(byte, output, rssi)) accepted++;
    assert(accepted == 1 && rssi == -100 && output.values == input.values);
  }
  StreamParser parser;
  int rssi = 0, accepted = 0;
  for (int packet = 0; packet < 3; packet++) {
    for (auto byte : frame) if (parser.feed(byte, output, rssi)) accepted++;
    if (parser.feed(176, output, rssi)) accepted++;
  }
  assert(accepted == 3 && rssi == -80);
  for (size_t i = 0; i < 10; i++) parser.feed(frame[i], output, rssi);
  parser.reset();
  for (auto byte : frame) assert(!parser.feed(byte, output, rssi));
  assert(parser.feed(176, output, rssi));
  // Noise stress with many false sync prefixes: bounded parser must survive.
  uint32_t random = 42;
  for (size_t i = 0; i < 100000; i++) {
    random = random * 1664525 + 1013904223;
    parser.feed(uint8_t(random >> 24), output, rssi);
  }
  parser.reset();
  for (auto byte : frame) parser.feed(byte, output, rssi);
  assert(parser.feed(156, output, rssi));

  SequenceTracker tracker;
  assert(tracker.accept(1, 50) == SequenceResult::FIRST);
  assert(tracker.missing == 0);  // Receiver starting late is not packet loss.
  assert(tracker.accept(1, 51) == SequenceResult::NEXT);
  assert(tracker.accept(1, 54) == SequenceResult::GAP && tracker.missing == 2);
  assert(tracker.accept(1, 54) == SequenceResult::DUPLICATE);
  assert(tracker.accept(1, 53) == SequenceResult::OLD);
  assert(tracker.accept(2, UINT32_MAX) == SequenceResult::NEW_BOOT);
  assert(tracker.accept(2, 0) == SequenceResult::NEXT && tracker.missing == 2);
  std::cout << "Protocol, corruption recovery and sequence tests passed\n";
}

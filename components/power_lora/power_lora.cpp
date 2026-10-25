#include "power_lora.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome::power_lora {
static const char *const TAG = "power_lora";

std::array<uint8_t, 8> PowerLoRa::registers_() const {
  // ADDH/L, REG0 9600 8N1 / 2.4kbps; REG1 128-byte subpacket / power;
  // CH18 = 868.125MHz; REG3 transparent / RSSI suffix / LBT off / WOR unused;
  // clear both crypt registers to make prior module settings deterministic.
  return {uint8_t(node_ >> 8), uint8_t(node_), 0x62,
          uint8_t(0x40 | (power_ == 13 ? 2 : 3)), 18, 0x80, 0, 0};
}

void PowerLoRa::setup() {
  m0_->setup(); m1_->setup(); aux_->setup();
  m0_->digital_write(true); m1_->digital_write(true);
  boot_ = random_uint32();
  reply_.reserve(11);
  transition_(State::ENTER_CONFIG, millis());
  publish_binary_(0, false); publish_binary_(1, false);
  ESP_LOGI(TAG, "Configuring E220 before enabling RF data; role=%s", transmitter_ ? "mock TX" : "RX");
}

void PowerLoRa::dump_config() {
  ESP_LOGCONFIG(TAG, "Power LoRa v1: %s, node=%u, 76-byte MOCK frames", transmitter_ ? "TX" : "RX", node_);
  ESP_LOGCONFIG(TAG, "868.125 MHz / 2.4 kbps / %u dBm / interval=%lu ms / stale=%lu ms",
                power_, (unsigned long) interval_, (unsigned long) stale_after_);
  LOG_PIN("  M0: ", m0_); LOG_PIN("  M1: ", m1_); LOG_PIN("  AUX: ", aux_);
}

void PowerLoRa::transition_(State state, uint32_t now) {
  state_ = state; state_since_ = now; reply_.clear();
}

bool PowerLoRa::aux_ready_(uint32_t now) const {
  return aux_high_ && uint32_t(now - aux_since_) >= 10;
}

void PowerLoRa::fail_(const char *reason) {
  ESP_LOGE(TAG, "%s; RF data disabled. Check wiring/power and reboot.", reason);
  m0_->digital_write(true); m1_->digital_write(true);
  state_ = State::FAILED;
  publish_binary_(0, false); publish_binary_(1, false);
  mark_failed();
}

void PowerLoRa::configuration_loop_(uint32_t now) {
  if (uint32_t(now - state_since_) > 5000) {
    fail_("E220 configuration/AUX timeout"); return;
  }
  const auto regs = registers_();
  if (state_ == State::ENTER_CONFIG) {
    if (uint32_t(now - state_since_) < 100 || !aux_ready_(now)) return;
    uint8_t ignored;
    while (available() && read_byte(&ignored)) {}
    std::array<uint8_t, 11> command{0xC2, 0, 8};
    std::copy(regs.begin(), regs.end(), command.begin() + 3);
    write_array(command.data(), command.size());
    transition_(State::WRITE_REPLY, now);
    return;
  }
  if (state_ == State::ENTER_NORMAL) {
    if (uint32_t(now - state_since_) < 100 || !aux_ready_(now)) return;
    transition_(State::RUNNING, now);
    last_tx_ = now;  // Also wait a full interval after every boot.
    publish_binary_(0, true);
    ESP_LOGI(TAG, "E220 registers verified; ready. First mock TX in %lu s", (unsigned long) (interval_ / 1000));
    return;
  }
  uint8_t byte;
  while (available() && reply_.size() < 11 && read_byte(&byte)) reply_.push_back(byte);
  const size_t expected = state_ == State::WRITE_REPLY ? 11 : 9;
  if (reply_.size() < expected) return;
  if (reply_.size() != expected || reply_[0] != 0xC1 || reply_[1] != 0 ||
      reply_[2] != expected - 3 || !std::equal(regs.begin(), regs.begin() + 6, reply_.begin() + 3)) {
    fail_("Unexpected E220 register response"); return;
  }
  if (!aux_ready_(now)) return;
  if (state_ == State::WRITE_REPLY) {
    const uint8_t command[]{0xC1, 0, 6};  // Independent read-back of readable registers.
    write_array(command, sizeof(command));
    transition_(State::READ_REPLY, now);
  } else {
    m0_->digital_write(false); m1_->digital_write(false);
    transition_(State::ENTER_NORMAL, now);
  }
}

void PowerLoRa::loop() {
  const uint32_t now = millis();
  bool high = aux_->digital_read();
  if (high != aux_high_) { aux_high_ = high; aux_since_ = now; }
  if (state_ == State::FAILED) return;
  if (state_ != State::RUNNING) { configuration_loop_(now); return; }
  if (uint32_t(now - last_byte_) > 2000) parser_.reset();
  uint8_t byte;
  // Limit work per loop, even with an electrically noisy UART.
  for (size_t i = 0; i < 256 && available() && read_byte(&byte); i++) {
    last_byte_ = now;
    Reading reading;
    int rssi;
    if (parser_.feed(byte, reading, rssi) && !transmitter_) receive_(reading, rssi, now);
  }
  if (transmitter_ && uint32_t(now - last_tx_) >= interval_) {
    if (aux_ready_(now)) {
      send_mock_(now);
    } else if (uint32_t(now - last_tx_) >= interval_ + 5000) {
      fail_("AUX stuck busy before transmission");
    }
  }
  if (!transmitter_ && received_ && !stale_ && uint32_t(now - last_rx_) >= stale_after_) {
    stale_ = true;
    publish_binary_(1, false);
    for (auto *s : measurements_) if (s != nullptr) s->publish_state(NAN);
    ESP_LOGW(TAG, "Link stale; measurements invalidated");
  }
  if (uint32_t(now - last_diag_) >= 1000) {
    last_diag_ = now; publish_diagnostics_(now);
  }
}

void PowerLoRa::send_mock_(uint32_t now) {
  Reading reading;
  reading.node = node_; reading.boot = boot_; reading.sequence = sequence_++;
  // Synthetic meter snapshot, not a synthetic raw P1 telegram. Deliberately
  // exercise import/export and reactive fields. Counters reset on TX reboot.
  const uint32_t step = tx_count_ % 12;
  reading.values = {
      12345678U + tx_count_ * 10U, 123456U + tx_count_,
      234567U + tx_count_ * 2U, 34567U + tx_count_,
      1200U + step * 30U, 50U, 180U, 20U,
      400U + step * 10U, 400U + step * 10U, 400U + step * 10U,
      0U, 0U, 50U,
      60U, 60U, 60U, 0U, 0U, 20U,
      2301U, 2312U, 2298U, 190U + step * 4U, 190U + step * 4U, 175U + step * 4U};
  auto frame = encode(reading);
  write_array(frame.data(), frame.size());
  last_tx_ = now; tx_count_++;
  publish_binary_(2, true);
  ESP_LOGI(TAG, "MOCK TX boot=%08lX seq=%lu bytes=%u (UART submission, not measured airtime)",
           (unsigned long) boot_, (unsigned long) reading.sequence, unsigned(frame.size()));
}

void PowerLoRa::receive_(const Reading &reading, int rssi, uint32_t now) {
  // This first-stage receiver deliberately accepts only mock packets. Live HAN
  // needs a separate entity namespace so test counters cannot pollute Energy.
  if (reading.node != node_ || reading.flags != FLAG_MOCK) return;
  auto result = tracker_.accept(reading.boot, reading.sequence);
  if (result == SequenceResult::DUPLICATE || result == SequenceResult::OLD) {
    duplicates_++; return;
  }
  if (result == SequenceResult::NEW_BOOT) restarts_++;
  received_ = true; stale_ = false; last_rx_ = now; rssi_ = rssi; rx_count_++;
  publish_binary_(1, true); publish_binary_(2, true);
  for (size_t i = 0; i < FIELD_COUNT; i++) {
    if (measurements_[i] == nullptr) continue;
    float divisor = i < 4 ? 1000.0f : (i >= 23 ? 100.0f : (i >= 20 ? 10.0f : 1.0f));
    measurements_[i]->publish_state(reading.values[i] == MISSING ? NAN : reading.values[i] / divisor);
  }
  ESP_LOGI(TAG, "MOCK RX boot=%08lX seq=%lu RSSI=%d dBm received=%lu missing=%lu",
           (unsigned long) reading.boot, (unsigned long) reading.sequence, rssi,
           (unsigned long) rx_count_, (unsigned long) tracker_.missing);
}

void PowerLoRa::publish_binary_(size_t index, bool value) {
  if (binaries_[index] != nullptr) binaries_[index]->publish_state(value);
}

void PowerLoRa::publish_diagnostics_(uint32_t now) {
  float values[]{float(rx_count_), float(tracker_.missing), float(parser_.errors),
                 float(duplicates_), float(restarts_),
                 received_ ? uint32_t(now - last_rx_) / 1000.0f : NAN,
                 received_ && !stale_ ? float(rssi_) : NAN, float(tx_count_)};
  for (size_t i = 0; i < diagnostics_.size(); i++)
    if (diagnostics_[i] != nullptr) diagnostics_[i]->publish_state(values[i]);
}
}  // namespace esphome::power_lora

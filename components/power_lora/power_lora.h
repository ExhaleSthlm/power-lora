#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "protocol.h"
#include <vector>

namespace esphome::power_lora {
class PowerLoRa : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_transmitter(bool value) { transmitter_ = value; }
  void set_m0_pin(GPIOPin *value) { m0_ = value; }
  void set_m1_pin(GPIOPin *value) { m1_ = value; }
  void set_aux_pin(GPIOPin *value) { aux_ = value; }
  void set_node_id(uint16_t value) { node_ = value; }
  void set_transmit_interval(uint32_t value) { interval_ = value; }
  void set_stale_after(uint32_t value) { stale_after_ = value; }
  void set_tx_power(uint8_t value) { power_ = value; }
  void set_measurement(size_t index, sensor::Sensor *value) { measurements_[index] = value; }
  void set_diagnostic(size_t index, sensor::Sensor *value) { diagnostics_[index] = value; }
  void set_binary(size_t index, binary_sensor::BinarySensor *value) { binaries_[index] = value; }

 protected:
  enum class State { ENTER_CONFIG, WRITE_REPLY, READ_REPLY, ENTER_NORMAL, RUNNING, FAILED };
  void transition_(State state, uint32_t now);
  void fail_(const char *reason);
  void configuration_loop_(uint32_t now);
  void send_mock_(uint32_t now);
  void receive_(const Reading &reading, int rssi, uint32_t now);
  void publish_diagnostics_(uint32_t now);
  void publish_binary_(size_t index, bool value);
  bool aux_ready_(uint32_t now) const;
  std::array<uint8_t, 8> registers_() const;
  GPIOPin *m0_{nullptr}, *m1_{nullptr}, *aux_{nullptr};
  bool transmitter_{false}, aux_high_{false}, received_{false}, stale_{true};
  uint16_t node_{1};
  uint8_t power_{10};
  State state_{State::ENTER_CONFIG};
  uint32_t state_since_{0}, aux_since_{0}, interval_{120000}, stale_after_{360000};
  uint32_t boot_{0}, sequence_{0}, last_tx_{0}, last_rx_{0}, last_byte_{0}, last_diag_{0};
  uint32_t tx_count_{0}, rx_count_{0}, duplicates_{0}, restarts_{0};
  int rssi_{-256};
  std::vector<uint8_t> reply_;
  StreamParser parser_;
  SequenceTracker tracker_;
  std::array<sensor::Sensor *, FIELD_COUNT> measurements_{};
  // received, missing, rejected, duplicates, restarts, age, RSSI, sent
  std::array<sensor::Sensor *, 8> diagnostics_{};
  // ready, link alive, mock data
  std::array<binary_sensor::BinarySensor *, 3> binaries_{};
};
}  // namespace esphome::power_lora

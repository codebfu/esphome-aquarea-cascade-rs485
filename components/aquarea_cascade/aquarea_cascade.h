#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"

#include <vector>

namespace esphome {
namespace aquarea_cascade {

enum class Command : uint8_t {
  OFF = 0,
  HEAT,
  COOL,
  ECS,
  HEAT_ECS,
  COOL_ECS,
};

class AquareaCascade : public Component, public uart::UARTDevice {
 public:
  void set_address(uint8_t address) { this->address_ = address; }
  void set_idle_ms(uint32_t idle_ms) { this->idle_ms_ = idle_ms; }
  void set_confirm_timeout_ms(uint32_t ms) { this->confirm_timeout_ms_ = ms; }
  void set_result_sensor(text_sensor::TextSensor *sensor) { this->result_sensor_ = sensor; }
  void set_phase_sensor(text_sensor::TextSensor *sensor) { this->phase_sensor_ = sensor; }
  void set_sniff_op_sensor(sensor::Sensor *sensor) { this->sniff_op_sensor_ = sensor; }
  void set_sniff_mode_sensor(sensor::Sensor *sensor) { this->sniff_mode_sensor_ = sensor; }
  void set_sniff_tank_sensor(sensor::Sensor *sensor) { this->sniff_tank_sensor_ = sensor; }
  void set_rx_frames_sensor(sensor::Sensor *sensor) { this->rx_frames_sensor_ = sensor; }

  void request_command(Command cmd);

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void dump_config() override;

 protected:
  enum class Phase : uint8_t {
    IDLE,
    WAIT_IDLE,
    WRITE,
    CONFIRM,
  };

  struct Target {
    bool check_op{false};
    bool check_mode{false};
    bool check_tank{false};
    uint16_t op{0};
    uint16_t mode{0};
    uint16_t tank{0};
  };

  void publish_result_(const std::string &value);
  void publish_phase_();
  void publish_sniff_();
  void start_command_(Command cmd);
  Target target_for_(Command cmd) const;
  const char *command_name_(Command cmd) const;
  const char *phase_name_() const;

  void feed_byte_(uint8_t b, uint32_t now_ms);
  void finalize_frame_(uint32_t now_ms);
  void handle_frame_(const uint8_t *data, size_t len);
  void maybe_update_sniffed_state_(uint16_t start_addr, const uint8_t *payload, size_t payload_len);

  bool bus_idle_(uint32_t now_ms) const;
  bool sniffed_matches_target_() const;
  void queue_writes_for_target_();
  bool send_next_write_();
  void drain_rx_();
  static uint16_t crc16_(const uint8_t *data, size_t len);
  static uint16_t u16_be_(const uint8_t *p);

  uint8_t address_{1};
  uint32_t idle_ms_{80};
  uint32_t confirm_timeout_ms_{60000};
  text_sensor::TextSensor *result_sensor_{nullptr};
  text_sensor::TextSensor *phase_sensor_{nullptr};
  sensor::Sensor *sniff_op_sensor_{nullptr};
  sensor::Sensor *sniff_mode_sensor_{nullptr};
  sensor::Sensor *sniff_tank_sensor_{nullptr};
  sensor::Sensor *rx_frames_sensor_{nullptr};

  Phase phase_{Phase::IDLE};
  Command pending_cmd_{Command::OFF};
  Target target_{};
  uint32_t command_started_ms_{0};
  uint32_t last_write_ms_{0};
  uint32_t last_rewrite_ms_{0};
  uint8_t match_streak_{0};
  uint8_t write_index_{0};
  std::vector<std::pair<uint16_t, uint16_t>> write_queue_;

  bool have_op_{false};
  bool have_mode_{false};
  bool have_tank_{false};
  uint16_t sniffed_op_{0};
  uint16_t sniffed_mode_{0};
  uint16_t sniffed_tank_{0};
  uint32_t last_match_poll_ms_{0};
  uint32_t rx_frames_{0};
  uint32_t last_debug_publish_ms_{0};

  static constexpr size_t kMaxFrame = 256;
  uint8_t frame_buf_[kMaxFrame]{};
  size_t frame_len_{0};
  uint32_t last_byte_ms_{0};
  // Idle is measured from last completed frame end (not every RX byte),
  // so dense CMH request/response pairs still leave a usable gap.
  uint32_t last_frame_end_ms_{0};
  uint32_t frame_gap_ms_{5};

  bool have_pending_read_{false};
  uint16_t pending_read_start_{0};
  uint16_t pending_read_count_{0};
};

}  // namespace aquarea_cascade
}  // namespace esphome

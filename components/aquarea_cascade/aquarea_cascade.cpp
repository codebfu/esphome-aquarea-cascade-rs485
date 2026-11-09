#include "aquarea_cascade.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aquarea_cascade {

static const char *const TAG = "aquarea_cascade";

void AquareaCascade::setup() {
  this->last_bus_activity_ms_ = millis();
  this->publish_result_("idle");
}

void AquareaCascade::dump_config() {
  ESP_LOGCONFIG(TAG, "Aquarea Cascade RS485 writer:");
  ESP_LOGCONFIG(TAG, "  Address: %u", this->address_);
  ESP_LOGCONFIG(TAG, "  Idle gap: %lu ms", (unsigned long) this->idle_ms_);
  ESP_LOGCONFIG(TAG, "  Confirm timeout: %lu ms", (unsigned long) this->confirm_timeout_ms_);
}

void AquareaCascade::request_command(Command cmd) {
  if (this->phase_ != Phase::IDLE) {
    ESP_LOGW(TAG, "Busy, ignoring new command");
    return;
  }
  this->start_command_(cmd);
}

void AquareaCascade::start_command_(Command cmd) {
  this->pending_cmd_ = cmd;
  this->target_ = this->target_for_(cmd);
  this->command_started_ms_ = millis();
  this->last_rewrite_ms_ = 0;
  this->match_streak_ = 0;
  this->write_index_ = 0;
  this->queue_writes_for_target_();
  this->phase_ = Phase::WAIT_IDLE;
  char buf[48];
  snprintf(buf, sizeof(buf), "running:%s", this->command_name_(cmd));
  this->publish_result_(buf);
  ESP_LOGI(TAG, "Start command %s (timeout %lu ms)", this->command_name_(cmd),
           (unsigned long) this->confirm_timeout_ms_);
}

AquareaCascade::Target AquareaCascade::target_for_(Command cmd) const {
  Target t{};
  switch (cmd) {
    case Command::OFF:
      t.check_op = true;
      t.op = 0;
      break;
    case Command::HEAT:
      t.check_op = t.check_mode = t.check_tank = true;
      t.op = 1;
      t.mode = 1;
      t.tank = 0;
      break;
    case Command::COOL:
      t.check_op = t.check_mode = t.check_tank = true;
      t.op = 1;
      t.mode = 5;
      t.tank = 0;
      break;
    case Command::ECS:
      t.check_op = t.check_mode = t.check_tank = true;
      t.op = 1;
      t.mode = 3;
      t.tank = 1;
      break;
    case Command::HEAT_ECS:
      t.check_op = t.check_mode = t.check_tank = true;
      t.op = 1;
      t.mode = 2;
      t.tank = 1;
      break;
    case Command::COOL_ECS:
      t.check_op = t.check_mode = t.check_tank = true;
      t.op = 1;
      t.mode = 4;
      t.tank = 1;
      break;
  }
  return t;
}

const char *AquareaCascade::command_name_(Command cmd) const {
  switch (cmd) {
    case Command::OFF:
      return "off";
    case Command::HEAT:
      return "heat";
    case Command::COOL:
      return "cool";
    case Command::ECS:
      return "ecs";
    case Command::HEAT_ECS:
      return "heat_ecs";
    case Command::COOL_ECS:
      return "cool_ecs";
  }
  return "unknown";
}

void AquareaCascade::queue_writes_for_target_() {
  this->write_queue_.clear();
  // Non-off: mode -> tank -> ON. Off: op only.
  if (this->pending_cmd_ == Command::OFF) {
    this->write_queue_.emplace_back(0, 0);
    return;
  }
  this->write_queue_.emplace_back(4, this->target_.mode);
  this->write_queue_.emplace_back(30, this->target_.tank);
  this->write_queue_.emplace_back(0, this->target_.op);
}

void AquareaCascade::publish_result_(const std::string &value) {
  if (this->result_sensor_ != nullptr) {
    this->result_sensor_->publish_state(value);
  }
}

bool AquareaCascade::bus_idle_(uint32_t now_ms) const {
  return (now_ms - this->last_bus_activity_ms_) >= this->idle_ms_;
}

bool AquareaCascade::sniffed_matches_target_() const {
  if (this->target_.check_op) {
    if (!this->have_op_ || this->sniffed_op_ != this->target_.op)
      return false;
  }
  if (this->target_.check_mode) {
    if (!this->have_mode_ || this->sniffed_mode_ != this->target_.mode)
      return false;
  }
  if (this->target_.check_tank) {
    if (!this->have_tank_ || this->sniffed_tank_ != this->target_.tank)
      return false;
  }
  return this->target_.check_op || this->target_.check_mode || this->target_.check_tank;
}

uint16_t AquareaCascade::crc16_(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

uint16_t AquareaCascade::u16_be_(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }

bool AquareaCascade::send_next_write_() {
  if (this->write_index_ >= this->write_queue_.size())
    return true;

  auto reg = this->write_queue_[this->write_index_].first;
  auto val = this->write_queue_[this->write_index_].second;
  uint8_t frame[8];
  frame[0] = this->address_;
  frame[1] = 0x06;
  frame[2] = (reg >> 8) & 0xFF;
  frame[3] = reg & 0xFF;
  frame[4] = (val >> 8) & 0xFF;
  frame[5] = val & 0xFF;
  uint16_t crc = crc16_(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  this->write_array(frame, sizeof(frame));
  this->flush();
  this->last_write_ms_ = millis();
  this->last_bus_activity_ms_ = this->last_write_ms_;
  ESP_LOGD(TAG, "TX FC06 id=%u reg=%u val=%u", this->address_, (unsigned) reg, (unsigned) val);
  this->write_index_++;
  return this->write_index_ >= this->write_queue_.size();
}

void AquareaCascade::feed_byte_(uint8_t b, uint32_t now_ms) {
  this->last_bus_activity_ms_ = now_ms;
  if (this->frame_len_ > 0 && (now_ms - this->last_byte_ms_) >= this->frame_gap_ms_) {
    this->finalize_frame_(now_ms);
  }
  this->last_byte_ms_ = now_ms;
  if (this->frame_len_ < kMaxFrame) {
    this->frame_buf_[this->frame_len_++] = b;
  } else {
    this->frame_len_ = 0;
  }
}

void AquareaCascade::finalize_frame_(uint32_t now_ms) {
  if (this->frame_len_ >= 4) {
    this->handle_frame_(this->frame_buf_, this->frame_len_);
  }
  this->frame_len_ = 0;
  (void) now_ms;
}

void AquareaCascade::handle_frame_(const uint8_t *data, size_t len) {
  if (len < 4)
    return;
  uint16_t crc_calc = crc16_(data, len - 2);
  uint16_t crc_frame = uint16_t(data[len - 2]) | (uint16_t(data[len - 1]) << 8);
  if (crc_calc != crc_frame)
    return;

  const uint8_t id = data[0];
  const uint8_t fc = data[1];

  // CMH FC03 request: id fc start_hi start_lo count_hi count_lo crc crc
  if (id == this->address_ && fc == 0x03 && len == 8) {
    this->pending_read_start_ = u16_be_(data + 2);
    this->pending_read_count_ = u16_be_(data + 4);
    this->have_pending_read_ = true;
    return;
  }

  // FC03 response: id fc bytecount data... crc crc
  if (id == this->address_ && fc == 0x03 && len >= 5) {
    uint8_t bytecount = data[2];
    if (len != size_t(5 + bytecount))
      return;
    if (!this->have_pending_read_) {
      // Heuristic: 90-byte payload = 45 regs; assume start 0 if outdoor looks sane later
      if (bytecount == 90) {
        this->maybe_update_sniffed_state_(0, data + 3, bytecount);
      }
      return;
    }
    uint16_t start = this->pending_read_start_;
    this->have_pending_read_ = false;
    this->maybe_update_sniffed_state_(start, data + 3, bytecount);
  }
}

void AquareaCascade::maybe_update_sniffed_state_(uint16_t start_addr, const uint8_t *payload,
                                                 size_t payload_len) {
  auto try_reg = [&](uint16_t reg, uint16_t &out, bool &have) {
    if (reg < start_addr)
      return;
    uint16_t idx = reg - start_addr;
    size_t off = size_t(idx) * 2;
    if (off + 1 >= payload_len)
      return;
    out = u16_be_(payload + off);
    have = true;
  };

  bool updated = false;
  uint16_t op = this->sniffed_op_, mode = this->sniffed_mode_, tank = this->sniffed_tank_;
  bool have_op = this->have_op_, have_mode = this->have_mode_, have_tank = this->have_tank_;

  try_reg(0, op, have_op);
  try_reg(4, mode, have_mode);
  try_reg(30, tank, have_tank);

  // Sanity for block0: outdoor (reg1) if present should be plausible when start==0
  if (start_addr == 0 && payload_len >= 4) {
    int16_t outdoor = int16_t(u16_be_(payload + 2));
    if (outdoor < -40 || outdoor > 60) {
      // Likely not status block 0 (truncated/collision) — ignore op/mode from this frame
      return;
    }
  }

  if (have_op && (!this->have_op_ || op != this->sniffed_op_))
    updated = true;
  if (have_mode && (!this->have_mode_ || mode != this->sniffed_mode_))
    updated = true;
  if (have_tank && (!this->have_tank_ || tank != this->sniffed_tank_))
    updated = true;

  this->sniffed_op_ = op;
  this->sniffed_mode_ = mode;
  this->sniffed_tank_ = tank;
  this->have_op_ = have_op;
  this->have_mode_ = have_mode;
  this->have_tank_ = have_tank;

  if (updated) {
    ESP_LOGD(TAG, "Sniffed HP1 op=%u mode=%u tank=%u", (unsigned) this->sniffed_op_,
             (unsigned) this->sniffed_mode_, (unsigned) this->sniffed_tank_);
  }

  if (this->phase_ == Phase::CONFIRM && this->sniffed_matches_target_()) {
    uint32_t now = millis();
    if (this->match_streak_ == 0 || (now - this->last_match_poll_ms_) > 50) {
      this->match_streak_++;
      this->last_match_poll_ms_ = now;
    }
  } else if (this->phase_ == Phase::CONFIRM) {
    this->match_streak_ = 0;
  }
}

void AquareaCascade::loop() {
  const uint32_t now = millis();

  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b))
      break;
    this->feed_byte_(b, now);
  }
  if (this->frame_len_ > 0 && (now - this->last_byte_ms_) >= this->frame_gap_ms_) {
    this->finalize_frame_(now);
  }

  if (this->phase_ == Phase::IDLE)
    return;

  if (now - this->command_started_ms_ >= this->confirm_timeout_ms_) {
    ESP_LOGW(TAG, "Command %s timed out", this->command_name_(this->pending_cmd_));
    this->publish_result_("timeout");
    this->phase_ = Phase::IDLE;
    return;
  }

  switch (this->phase_) {
    case Phase::WAIT_IDLE:
      if (this->bus_idle_(now)) {
        this->write_index_ = 0;
        this->phase_ = Phase::WRITE;
      }
      break;

    case Phase::WRITE:
      // Space writes slightly; require idle before each frame
      if (!this->bus_idle_(now) && this->write_index_ > 0 && (now - this->last_write_ms_) < 50)
        break;
      if (!this->bus_idle_(now) && this->write_index_ == 0)
        break;
      if (this->send_next_write_()) {
        this->phase_ = Phase::CONFIRM;
        this->match_streak_ = 0;
        this->last_rewrite_ms_ = now;
      } else {
        // wait a bit before next register write
        this->last_bus_activity_ms_ = now;
      }
      break;

    case Phase::CONFIRM:
      if (this->match_streak_ >= 2) {
        uint32_t elapsed = now - this->command_started_ms_;
        char buf[48];
        snprintf(buf, sizeof(buf), "ok:%s:%lums", this->command_name_(this->pending_cmd_),
                 (unsigned long) elapsed);
        ESP_LOGI(TAG, "Confirmed %s in %lu ms", this->command_name_(this->pending_cmd_),
                 (unsigned long) elapsed);
        this->publish_result_(buf);
        this->phase_ = Phase::IDLE;
        break;
      }
      // Rewrite every ~2s if not yet matching (smart, not flood)
      if (now - this->last_rewrite_ms_ >= 2000) {
        ESP_LOGD(TAG, "Rewrite %s (no stable match yet)", this->command_name_(this->pending_cmd_));
        this->queue_writes_for_target_();
        this->write_index_ = 0;
        this->phase_ = Phase::WAIT_IDLE;
        this->last_rewrite_ms_ = now;
      }
      break;

    case Phase::IDLE:
      break;
  }
}

}  // namespace aquarea_cascade
}  // namespace esphome

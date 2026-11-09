#include "aquarea_cascade.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <esp_system.h>
#include <esp_ota_ops.h>

namespace esphome {
namespace aquarea_cascade {

static const char *const TAG = "aquarea_cascade";

static const char *reset_reason_str_() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "ext";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "other";
  }
}

void AquareaCascade::setup() {
  // Prevent esp-idf app rollback if we crash later during bus tests.
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (running != nullptr && esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_ota_mark_app_valid_cancel_rollback();
      ESP_LOGI(TAG, "OTA app marked valid (cancel rollback)");
    }
  }

  this->last_frame_end_ms_ = millis();
  this->publish_result_("idle");
  this->publish_phase_();
  if (this->reset_reason_sensor_ != nullptr) {
    this->reset_reason_sensor_->publish_state(reset_reason_str_());
  }
  ESP_LOGI(TAG, "Boot reset_reason=%s dry_run=%d", reset_reason_str_(), (int) this->dry_run_);
}

void AquareaCascade::dump_config() {
  ESP_LOGCONFIG(TAG, "Aquarea Cascade RS485 writer:");
  ESP_LOGCONFIG(TAG, "  Address: %u", this->address_);
  ESP_LOGCONFIG(TAG, "  Idle gap: %lu ms", (unsigned long) this->idle_ms_);
  ESP_LOGCONFIG(TAG, "  Confirm timeout: %lu ms", (unsigned long) this->confirm_timeout_ms_);
}

void AquareaCascade::request_command(Command cmd) {
  // Safe to call from button/API thread: only set a flag for loop().
  this->queued_cmd_ = cmd;
  this->queued_cmd_valid_ = true;
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
  this->publish_phase_();
  ESP_LOGI(TAG, "Start command %s (timeout %lu ms, idle %lu ms)", this->command_name_(cmd),
           (unsigned long) this->confirm_timeout_ms_, (unsigned long) this->idle_ms_);
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

const char *AquareaCascade::phase_name_() const {
  switch (this->phase_) {
    case Phase::IDLE:
      return "idle";
    case Phase::WAIT_IDLE:
      return "wait_idle";
    case Phase::WRITE:
      return "write";
    case Phase::CONFIRM:
      return "confirm";
  }
  return "?";
}

void AquareaCascade::queue_writes_for_target_() {
  this->write_count_ = 0;
  auto push = [&](uint16_t reg, uint16_t val) {
    if (this->write_count_ >= 4)
      return;
    this->write_regs_[this->write_count_] = reg;
    this->write_vals_[this->write_count_] = val;
    this->write_count_++;
  };
  if (this->pending_cmd_ == Command::OFF) {
    push(0, 0);
    return;
  }
  push(4, this->target_.mode);
  push(30, this->target_.tank);
  push(0, this->target_.op);
}

void AquareaCascade::publish_result_(const std::string &value) {
  if (this->result_sensor_ != nullptr) {
    this->result_sensor_->publish_state(value);
  }
}

void AquareaCascade::publish_phase_() {
  if (this->phase_sensor_ != nullptr) {
    this->phase_sensor_->publish_state(this->phase_name_());
  }
}

void AquareaCascade::publish_sniff_() {
  if (this->sniff_op_sensor_ != nullptr && this->have_op_)
    this->sniff_op_sensor_->publish_state(this->sniffed_op_);
  if (this->sniff_mode_sensor_ != nullptr && this->have_mode_)
    this->sniff_mode_sensor_->publish_state(this->sniffed_mode_);
  if (this->sniff_tank_sensor_ != nullptr && this->have_tank_)
    this->sniff_tank_sensor_->publish_state(this->sniffed_tank_);
  if (this->rx_frames_sensor_ != nullptr)
    this->rx_frames_sensor_->publish_state(this->rx_frames_);
}

bool AquareaCascade::bus_idle_(uint32_t now_ms) const {
  // Prefer silence after a completed RTU frame; also treat "no bytes yet in buffer"
  // so we do not wait forever if the bus is quiet.
  if (this->frame_len_ > 0)
    return false;
  return (now_ms - this->last_frame_end_ms_) >= this->idle_ms_;
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

void AquareaCascade::drain_rx_() {
  uint8_t b;
  uint16_t n = 0;
  while (this->available() && n < 512) {
    if (!this->read_byte(&b))
      break;
    n++;
  }
  this->frame_len_ = 0;
}

bool AquareaCascade::send_next_write_() {
  if (this->write_index_ >= this->write_count_)
    return true;

  auto reg = this->write_regs_[this->write_index_];
  auto val = this->write_vals_[this->write_index_];

  if (this->dry_run_) {
    ESP_LOGI(TAG, "DRY_RUN skip TX FC06 id=%u reg=%u val=%u", this->address_, (unsigned) reg,
             (unsigned) val);
    this->last_write_ms_ = millis();
    this->tx_holdoff_until_ms_ = this->last_write_ms_ + 15;
    this->last_frame_end_ms_ = this->last_write_ms_;
    this->write_index_++;
    return this->write_index_ >= this->write_count_;
  }

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

  this->drain_rx_();
  this->write_array(frame, sizeof(frame));
  this->last_write_ms_ = millis();
  // Non-blocking holdoff (~12 ms for 8 bytes @ 9600) instead of delayMicroseconds.
  this->tx_holdoff_until_ms_ = this->last_write_ms_ + 15;
  this->last_frame_end_ms_ = this->last_write_ms_;
  this->frame_len_ = 0;
  ESP_LOGI(TAG, "TX FC06 id=%u reg=%u val=%u", this->address_, (unsigned) reg, (unsigned) val);
  this->write_index_++;
  return this->write_index_ >= this->write_count_;
}

void AquareaCascade::feed_byte_(uint8_t b, uint32_t now_ms) {
  if (this->frame_len_ > 0 && (now_ms - this->last_byte_ms_) >= this->frame_gap_ms_) {
    this->finalize_frame_(now_ms);
  }
  this->last_byte_ms_ = now_ms;
  if (this->frame_len_ < kMaxFrame) {
    this->frame_buf_[this->frame_len_++] = b;
  } else {
    this->frame_len_ = 0;
    this->frame_buf_[this->frame_len_++] = b;
  }

  // Cut frames as soon as a complete Modbus RTU PDU is present — CMH
  // request→response turnaround can be <5 ms, so gap-only framing merges them.
  if (this->frame_len_ >= 8) {
    const uint8_t fc = this->frame_buf_[1];
    if ((fc == 0x03 || fc == 0x06 || fc == 0x10) && this->frame_len_ == 8) {
      uint16_t crc_calc = crc16_(this->frame_buf_, 6);
      uint16_t crc_frame = uint16_t(this->frame_buf_[6]) | (uint16_t(this->frame_buf_[7]) << 8);
      if (crc_calc == crc_frame) {
        this->finalize_frame_(now_ms);
        return;
      }
    }
  }
  if (this->frame_len_ >= 5 && this->frame_buf_[1] == 0x03) {
    uint8_t bytecount = this->frame_buf_[2];
    size_t need = size_t(5) + bytecount;
    if (bytecount > 0 && bytecount <= 250 && this->frame_len_ == need) {
      uint16_t crc_calc = crc16_(this->frame_buf_, need - 2);
      uint16_t crc_frame =
          uint16_t(this->frame_buf_[need - 2]) | (uint16_t(this->frame_buf_[need - 1]) << 8);
      if (crc_calc == crc_frame) {
        this->finalize_frame_(now_ms);
      }
    }
  }
}

void AquareaCascade::finalize_frame_(uint32_t now_ms) {
  if (this->frame_len_ >= 4) {
    this->handle_frame_(this->frame_buf_, this->frame_len_);
  }
  this->frame_len_ = 0;
  this->last_frame_end_ms_ = now_ms;
}

void AquareaCascade::handle_frame_(const uint8_t *data, size_t len) {
  if (len < 4)
    return;
  uint16_t crc_calc = crc16_(data, len - 2);
  uint16_t crc_frame = uint16_t(data[len - 2]) | (uint16_t(data[len - 1]) << 8);
  if (crc_calc != crc_frame)
    return;

  this->rx_frames_++;
  const uint8_t id = data[0];
  const uint8_t fc = data[1];

  if (id == this->address_ && fc == 0x03 && len == 8) {
    this->pending_read_start_ = u16_be_(data + 2);
    this->pending_read_count_ = u16_be_(data + 4);
    this->have_pending_read_ = true;
    return;
  }

  if (id == this->address_ && fc == 0x03 && len >= 5) {
    uint8_t bytecount = data[2];
    if (len != size_t(5 + bytecount))
      return;
    uint16_t start = 0;
    if (this->have_pending_read_) {
      start = this->pending_read_start_;
      this->have_pending_read_ = false;
    } else if (bytecount != 90) {
      return;
    }
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

  uint16_t op = this->sniffed_op_, mode = this->sniffed_mode_, tank = this->sniffed_tank_;
  bool have_op = this->have_op_, have_mode = this->have_mode_, have_tank = this->have_tank_;

  try_reg(0, op, have_op);
  try_reg(4, mode, have_mode);
  try_reg(30, tank, have_tank);

  if (start_addr == 0 && payload_len >= 4) {
    int16_t outdoor = int16_t(u16_be_(payload + 2));
    if (outdoor < -40 || outdoor > 60) {
      return;
    }
  }

  bool updated = false;
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
    ESP_LOGI(TAG, "Sniffed HP1 op=%u mode=%u tank=%u (frames=%lu)", (unsigned) this->sniffed_op_,
             (unsigned) this->sniffed_mode_, (unsigned) this->sniffed_tank_,
             (unsigned long) this->rx_frames_);
    this->publish_sniff_();
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

  if (this->queued_cmd_valid_) {
    this->queued_cmd_valid_ = false;
    Command cmd = this->queued_cmd_;
    if (this->phase_ != Phase::IDLE) {
      ESP_LOGW(TAG, "Busy (%s), ignoring queued command", this->phase_name_());
    } else {
      this->start_command_(cmd);
    }
  }

  // Cap bytes per loop to avoid starving WiFi / WDT under noisy RX.
  uint16_t budget = 128;
  while (budget-- && this->available()) {
    uint8_t b;
    if (!this->read_byte(&b))
      break;
    this->feed_byte_(b, now);
  }
  if (this->frame_len_ > 0 && (now - this->last_byte_ms_) >= this->frame_gap_ms_) {
    this->finalize_frame_(now);
  }

  if (now - this->last_debug_publish_ms_ >= 5000) {
    this->last_debug_publish_ms_ = now;
    this->publish_phase_();
    this->publish_sniff_();
    ESP_LOGD(TAG, "dbg phase=%s frames=%lu idle_ago=%lu have_op=%d", this->phase_name_(),
             (unsigned long) this->rx_frames_, (unsigned long) (now - this->last_frame_end_ms_),
             (int) this->have_op_);
  }

  if (this->phase_ == Phase::IDLE)
    return;

  if (now - this->command_started_ms_ >= this->confirm_timeout_ms_) {
    ESP_LOGW(TAG, "Command %s timed out (phase=%s frames=%lu)", this->command_name_(this->pending_cmd_),
             this->phase_name_(), (unsigned long) this->rx_frames_);
    this->publish_result_("timeout");
    this->phase_ = Phase::IDLE;
    this->publish_phase_();
    return;
  }

  switch (this->phase_) {
    case Phase::WAIT_IDLE:
      if (this->bus_idle_(now)) {
        this->write_index_ = 0;
        this->phase_ = Phase::WRITE;
        this->publish_phase_();
        ESP_LOGI(TAG, "Bus idle — writing");
      }
      break;

    case Phase::WRITE: {
      if (now < this->tx_holdoff_until_ms_)
        break;
      // After first register, only need a short gap (~50ms) before the next.
      uint32_t need = this->write_index_ == 0 ? this->idle_ms_ : 50;
      if ((now - this->last_frame_end_ms_) < need || this->frame_len_ > 0)
        break;
      if (this->send_next_write_()) {
        this->phase_ = Phase::CONFIRM;
        this->match_streak_ = 0;
        this->last_rewrite_ms_ = now;
        this->publish_phase_();
        ESP_LOGI(TAG, "Writes done — confirming via CMH sniff");
      }
      break;
    }

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
        this->publish_phase_();
        break;
      }
      if (now - this->last_rewrite_ms_ >= 2000) {
        ESP_LOGI(TAG, "Rewrite %s (no stable match, op=%d mode=%d tank=%d)",
                 this->command_name_(this->pending_cmd_),
                 this->have_op_ ? (int) this->sniffed_op_ : -1,
                 this->have_mode_ ? (int) this->sniffed_mode_ : -1,
                 this->have_tank_ ? (int) this->sniffed_tank_ : -1);
        this->queue_writes_for_target_();
        this->write_index_ = 0;
        this->phase_ = Phase::WAIT_IDLE;
        this->last_rewrite_ms_ = now;
        this->publish_phase_();
      }
      break;

    case Phase::IDLE:
      break;
  }
}

}  // namespace aquarea_cascade
}  // namespace esphome

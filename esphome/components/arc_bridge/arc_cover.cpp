#include "arc_cover.h"
#include "arc_bridge.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_cover";

cover::CoverTraits ARCCover::get_traits() {
  cover::CoverTraits traits;
  traits.set_supports_position(true);
  traits.set_supports_stop(true);
  return traits;
}

void ARCCover::publish_raw_position(int device_pos) {
  if (device_pos < 0) device_pos = 0;
  if (device_pos > 100) device_pos = 100;

  // Cache last known position (0–100)
  this->last_known_pos_ = device_pos;

  // ARC: 0=open, 100=closed → HA: 1.0=open, 0.0=closed
  float ha_pos = 1.0f - (static_cast<float>(device_pos) / 100.0f);

  ESP_LOGD("arc_cover", "[%s] device_pos=%d -> ha_pos=%.2f",
           this->blind_id_.c_str(), device_pos, ha_pos);

  this->position = ha_pos;
  this->publish_state();
}

void ARCCover::publish_unavailable() {
  ESP_LOGW("arc_cover", "[%s] marking as unavailable", this->blind_id_.c_str());
  this->status_set_error();  // tells HA entity is unavailable
  this->publish_state();     // refresh HA
}

void ARCCover::publish_link_quality(float value) {
  if (this->link_sensor_ != nullptr) {
    this->link_sensor_->publish_state(value);
    ESP_LOGD("arc_cover", "[%s] Link quality: %.1f%%", this->blind_id_.c_str(), value);
  }
}

void ARCCover::set_available(bool available) {
  if (!available) {
    this->status_set_error();   // mark unavailable in HA
    ESP_LOGW("arc_cover", "[%s] marked unavailable", this->blind_id_.c_str());
  } else {
    this->status_clear_error(); // restore availability
    if (this->last_known_pos_ >= 0)
      this->publish_raw_position(this->last_known_pos_);
    ESP_LOGD("arc_cover", "[%s] marked available", this->blind_id_.c_str());
  }
}

void ARCCover::control(const cover::CoverCall &call) {
  if (this->bridge_ == nullptr) {
    ESP_LOGW(TAG, "[%s] No ARC bridge associated", this->blind_id_.c_str());
    return;
  }

  // 🛑 prevent any movement during startup guard
  if (!this->bridge_->is_startup_guard_cleared()) {
    ESP_LOGW(TAG, "[%s] Ignoring command during startup guard period", this->blind_id_.c_str());
    return;
  }

  if (call.get_stop()) {
    this->bridge_->send_stop(this->blind_id_);
    return;
  }

  if (call.get_position().has_value()) {
    float p = *call.get_position();
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;

    uint8_t arc_percent;
    if (this->invert_position_) {
      arc_percent = static_cast<uint8_t>(std::round(p * 100.0f));
    } else {
      arc_percent = static_cast<uint8_t>(std::round((1.0f - p) * 100.0f));
    }

    ESP_LOGD(TAG, "[%s] control pos=%.2f -> arc_percent=%d", this->blind_id_.c_str(), p, arc_percent);

    if (arc_percent >= 100)
      this->bridge_->send_close(this->blind_id_);
    else if (arc_percent <= 0)
      this->bridge_->send_open(this->blind_id_);
    else
      this->bridge_->send_move(this->blind_id_, arc_percent);
  }
}

}  // namespace arc_bridge
}  // namespace esphome

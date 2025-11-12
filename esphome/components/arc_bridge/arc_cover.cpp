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
  // Handle missing or invalid position
  if (device_pos < 0 || device_pos > 100) {
    ESP_LOGW("arc_cover", "[%s] invalid/missing position (%d) -> marking unknown", this->blind_id_.c_str(), device_pos);

    this->position = NAN;
    this->set_has_state(false);   // 👈 mark as unavailable / grayed out in HA
    this->publish_state();
    return;
  }

  // Compute HA position (invert for 0=open, 100=closed)
  float ha_pos = 1.0f - (static_cast<float>(device_pos) / 100.0f);

  // Sanity check
  if (ha_pos < 0.0f || ha_pos > 1.0f || std::isnan(ha_pos)) {
    ESP_LOGW("arc_cover", "[%s] invalid ha_pos %.2f -> ignoring", this->blind_id_.c_str(), ha_pos);
    return;
  }

  // Avoid re-publishing unchanged values
  if (this->has_state() && !std::isnan(this->position) &&
      fabs(this->position - ha_pos) < 0.005f) {
    ESP_LOGV("arc_cover", "[%s] ha_pos=%.2f unchanged -> no publish", this->blind_id_.c_str(), ha_pos);
    return;
  }

  // All good — restore availability and publish
  this->set_has_state(true);      // 👈 restore visibility in HA
  this->position = ha_pos;
  ESP_LOGD("arc_cover", "[%s] device_pos=%d -> ha_pos=%.2f", this->blind_id_.c_str(), device_pos, ha_pos);
  this->publish_state();
}

void ARCCover::publish_unavailable() {
  ESP_LOGW("arc_cover", "[%s] marking as unavailable", this->blind_id_.c_str());
  // Mark the entity as unavailable using the component status flag
  this->status_set_warning();      // sets HA entity to unavailable/warning
  //this->position = NAN;            // clear any valid position
  this->publish_state("unavailable");           // trigger HA update
}

void ARCCover::publish_link_quality(float value) {
  if (this->link_sensor_ != nullptr) {
    this->link_sensor_->publish_state(value);
    ESP_LOGD("arc_cover", "[%s] Link quality: %.1f%%", this->blind_id_.c_str(), value);
  }
}

void ARCCover::set_available(bool available) {
  if (!available) {
    this->status_set_warning();
    this->position = NAN;
    this->publish_state();
    ESP_LOGW("arc_cover", "[%s] marked unavailable", this->blind_id_.c_str());
  } else {
    this->status_clear_warning();
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

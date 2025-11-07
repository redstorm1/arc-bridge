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

  float ha_pos;
  // ARC uses 0=open, 100=closed
  if (this->invert_position_) {
    ha_pos = static_cast<float>(device_pos) / 100.0f;
  } else {
    ha_pos = 1.0f - (static_cast<float>(device_pos) / 100.0f);
  }

  ESP_LOGD(TAG, "[%s] device_pos=%d -> ha_pos=%.2f (invert=%d)",
           this->blind_id_.c_str(), device_pos, ha_pos, this->invert_position_);
  this->publish_state(ha_pos);
}

void ARCCover::control(const cover::CoverCall &call) {
  if (this->bridge_ == nullptr) {
    ESP_LOGW(TAG, "[%s] No ARC bridge associated", this->blind_id_.c_str());
    return;
  }

  if (call.get_stop()) {
    this->bridge_->send_stop_command(this->blind_id_);
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
      this->bridge_->send_close_command(this->blind_id_);
    else if (arc_percent <= 0)
      this->bridge_->send_open_command(this->blind_id_);
    else
      this->bridge_->send_move_command(this->blind_id_, arc_percent);
  }
}

}  // namespace arc_bridge
}  // namespace esphome

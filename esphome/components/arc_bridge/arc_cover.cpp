#include "arc_cover.h"
#include "arc_bridge.h"
#include "esphome/core/log.h"

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_cover";

cover::CoverTraits ARCCover::get_traits() {
  cover::CoverTraits t;
  t.set_supports_position(true);
  t.set_supports_stop(true);
  t.set_is_assumed_state(false);
  return t;
}

void ARCCover::control(const cover::CoverCall &call) {
  if (!bridge_) {
    ESP_LOGW(TAG, "No bridge linked for cover id='%s'", id_.c_str());
    return;
  }

  if (call.get_stop()) {
    bridge_->send_stop(id_);
    return;
  }

  if (call.get_position().has_value()) {
    float pos = *call.get_position();  // 1=open, 0=closed
    uint8_t arc_pos = static_cast<uint8_t>(std::round((1.0f - pos) * 100.0f));
    bridge_->send_move(id_, arc_pos);
    return;
  }
}

void ARCCover::publish_raw(int arc_pos) {
  if (arc_pos < 0) arc_pos = 0;
  if (arc_pos > 100) arc_pos = 100;
  float ha_pos = 1.0f - (arc_pos / 100.0f);  // 0=open(arc) → 1=open(HA)
  this->publish_state(ha_pos);
  ESP_LOGD(TAG, "Cover %s position updated: %d (HA=%.2f)", id_.c_str(), arc_pos, ha_pos);
}

}  // namespace arc_bridge
}  // namespace esphome

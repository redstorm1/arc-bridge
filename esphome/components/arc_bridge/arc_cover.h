#pragma once
#include "esphome/core/component.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace arc_bridge {

class ARCBridgeComponent;  // forward declare

class ARCCover : public cover::Cover, public Component {
 public:
  // link this cover to the parent ARC bridge
  void set_bridge(ARCBridgeComponent *bridge) { this->bridge_ = bridge; }

  // blind ID, e.g. "USZ"
  void set_blind_id(const std::string &id) { this->blind_id_ = id; }
  const std::string &get_blind_id() const { return this->blind_id_; }

  // optional invert flag
  void set_invert_position(bool invert) { this->invert_position_ = invert; }

  // publishers
  void publish_raw_position(int device_pos);
  void publish_unavailable();
  void publish_link_quality(float value);

  // allow bridge to attach a sensor
  void set_link_sensor(sensor::Sensor *s) { this->link_sensor_ = s; }

    void set_available(bool available) {
    // Toggle Home Assistant entity availability
    this->mark_has_state(available);
    if (!available) {
        ESP_LOGW("arc_cover", "[%s] marked unavailable", this->blind_id_.c_str());
        this->publish_unavailable();
    }
}


  // cover traits and commands
  cover::CoverTraits get_traits() override;
  void control(const cover::CoverCall &call) override;

 protected:
  ARCBridgeComponent *bridge_{nullptr};
  std::string blind_id_;
  bool invert_position_{false};

  // optional linked sensor for RSSI / link quality
  sensor::Sensor *link_sensor_{nullptr};
};

}  // namespace arc_bridge
}  // namespace esphome

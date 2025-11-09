#pragma once
#include "esphome/core/component.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace arc_bridge {

class ARCBridgeComponent;  // forward declare

class ARCCover : public cover::Cover, public Component {
 public:
  void set_bridge(ARCBridgeComponent *bridge) { this->bridge_ = bridge; }

  void set_blind_id(const std::string &id) { this->blind_id_ = id; }
  const std::string &get_blind_id() const { return this->blind_id_; }

  void set_invert_position(bool invert) { this->invert_position_ = invert; }

  // publishers
  void publish_raw_position(int device_pos);
  void publish_unavailable();
  void publish_link_quality(float value);

  void set_link_sensor(sensor::Sensor *s) { this->link_sensor_ = s; }

  // Correct availability handling for HA
  void set_available(bool available);

  cover::CoverTraits get_traits() override;
  void control(const cover::CoverCall &call) override;

 protected:
  ARCBridgeComponent *bridge_{nullptr};
  std::string blind_id_;
  bool invert_position_{false};

  // optional linked sensor for RSSI / link quality
  sensor::Sensor *link_sensor_{nullptr};

  // cache last known position for availability restore
  int last_known_pos_{-1};
};

}  // namespace arc_bridge
}  // namespace esphome

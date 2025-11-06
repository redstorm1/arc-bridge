#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace arc_bridge {

class ARCBlind;

// --------------------------------------------------------------------
// Main ARC Bridge component (handles UART comms and blind registry)
// --------------------------------------------------------------------
class ARCBridgeComponent : public Component, public uart::UARTDevice {
 public:
  void add_blind(ARCBlind *blind) { blinds_.push_back(blind); }
  void map_lq_sensor(const std::string &id, sensor::Sensor *s) { lq_map_[id] = s; }
  void map_status_sensor(const std::string &id, text_sensor::TextSensor *s) { status_map_[id] = s; }

  void setup() override {}
  void loop() override {}
  float get_setup_priority() const override { return setup_priority::DATA; }

 private:
  std::vector<ARCBlind *> blinds_;
  std::map<std::string, sensor::Sensor *> lq_map_;
  std::map<std::string, text_sensor::TextSensor *> status_map_;
};

// --------------------------------------------------------------------
// Individual ARC Blind entity (acts as a Cover)
// --------------------------------------------------------------------
class ARCBlind : public cover::Cover, public Component {
 public:
  void set_blind_id(const std::string &id) { blind_id_ = id; }
  void set_name(const std::string &name) { name_ = name; }

 protected:
  cover::CoverTraits get_traits() override {
    cover::CoverTraits traits;
    traits.set_is_optimistic(false);
    traits.set_supports_position(true);
    return traits;
  }

  void control(const cover::CoverCall &call) override {
    // TODO: send ARC protocol command here later
  }

 private:
  std::string blind_id_;
  std::string name_;
};

}  // namespace arc_bridge
}  // namespace esphome


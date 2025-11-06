#pragma once
#include "esphome.h"
#include <map>

namespace esphome {
namespace arc_bridge {

class ARCBlind;

class ARCBridgeComponent : public Component, public uart::UARTDevice {
 public:
  void add_blind(ARCBlind *blind);
  void map_lq_sensor(const std::string &id, sensor::Sensor *sensor);
  void map_status_sensor(const std::string &id, text_sensor::TextSensor *sensor);
};

class ARCBlind : public cover::Cover, public Component {
 public:
  void set_blind_id(const std::string &id) { blind_id_ = id; }
  void set_name(const std::string &name) { name_ = name; }
 private:
  std::string blind_id_;
  std::string name_;
};

}  // namespace arc_bridge
}  // namespace esphome


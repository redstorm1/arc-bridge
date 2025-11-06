#pragma once
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include <map>
#include <string>

namespace esphome {

// Forward declarations only — do not include full headers
namespace sensor { class Sensor; }
namespace text_sensor { class TextSensor; }
namespace cover { class Cover; }

namespace arc_bridge {

class ARCBlind;

// Main UART bridge
class ARCBridgeComponent : public Component, public uart::UARTDevice {
 public:
  void add_blind(ARCBlind *blind);
  void map_lq_sensor(const std::string &id, esphome::sensor::Sensor *sensor);
  void map_status_sensor(const std::string &id, esphome::text_sensor::TextSensor *sensor);

 protected:
  std::map<std::string, ARCBlind *> blinds_;
  std::map<std::string, esphome::sensor::Sensor *> lq_sensors_;
  std::map<std::string, esphome::text_sensor::TextSensor *> status_sensors_;
};

// Blind (cover) entity
class ARCBlind : public esphome::Component {
 public:
  void set_blind_id(const std::string &id) { blind_id_ = id; }
  void set_name(const std::string &name) { name_ = name; }

 private:
  std::string blind_id_;
  std::string name_;
};

}  // namespace arc_bridge
}  // namespace esphome

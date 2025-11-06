#pragma once
#include "esphome.h"
#include <map>
#include <string>

namespace esphome {
namespace sensor { class Sensor; }        // forward declaration only
namespace text_sensor { class TextSensor; }
namespace cover { class Cover; }

namespace arc_bridge {

class ARCBlind;  // forward declaration

class ARCBridgeComponent : public Component, public uart::UARTDevice {
 public:
  ARCBridgeComponent() = default;

  void setup() override;
  void loop() override;
  void dump_config() override;

  void add_blind(ARCBlind *blind);
  void map_lq_sensor(const std::string &id, sensor::Sensor *s);
  void map_status_sensor(const std::string &id, text_sensor::TextSensor *t);

 protected:
  std::map<std::string, ARCBlind *> blinds_;
  std::map<std::string, sensor::Sensor *> lq_sensors_;
  std::map<std::string, text_sensor::TextSensor *> status_sensors_;
};

class ARCBlind : public cover::Cover, public Component {
 public:
  ARCBlind() = default;

  void setup() override;
  void dump_config() override;
  cover::CoverTraits get_traits() override;

  void set_blind_id(const std::string &id) { blind_id_ = id; }
  void set_name(const std::string &name) { name_ = name; }

 protected:
  std::string blind_id_;
  std::string name_;
};

}  // namespace arc_bridge
}  // namespace esphome

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <cstdint>
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
  void add_blind(ARCBlind *blind);
  void map_lq_sensor(const std::string &id, sensor::Sensor *s);
  void map_status_sensor(const std::string &id, text_sensor::TextSensor *s);

  void send_move_command(const std::string &blind_id, uint8_t percent);
  void send_open_command(const std::string &blind_id);
  void send_close_command(const std::string &blind_id);
  void send_stop_command(const std::string &blind_id);
  void send_position_query(const std::string &blind_id);
  void handle_incoming_frame(const std::string &frame);

  void setup() override;
  void loop() override;

 private:
  void send_simple_command_(const std::string &blind_id, char command,
                            const std::string &payload = std::string());
  ARCBlind *find_blind_by_id(const std::string &id);

  std::vector<ARCBlind *> blinds_;
  std::map<std::string, sensor::Sensor *> lq_map_;
  std::map<std::string, text_sensor::TextSensor *> status_map_;

  // startup guard tracking
  uint32_t boot_millis_{0};
  bool startup_guard_cleared_{false};
};

// --------------------------------------------------------------------
// Individual ARC Blind entity (acts as a Cover)
// --------------------------------------------------------------------
class ARCBlind : public cover::Cover, public Component {
 public:
   void set_blind_id(const std::string &id) { blind_id_ = id; }
   const std::string &get_blind_id() const { return blind_id_; }
   void set_name(const std::string &name);
   void set_parent(ARCBridgeComponent *parent) { parent_ = parent; }

+  // allow configuring position inversion (device 0=open vs 0=closed)
+  void set_invert_position(bool invert) { invert_position_ = invert; }
+
+  // accept raw device position (0..100) and convert/publish to HA semantics
+  void publish_raw_position(int device_pos);

   // lifecycle
   void setup() override;

   // publish a position received from the bridge (0.0..1.0 HA semantics)
   void publish_position(float position);

   // clear the startup ignore guard (called by bridge timeout or on first pos)
   void clear_startup_guard();

 protected:
  cover::CoverTraits get_traits() override {
    cover::CoverTraits traits{};
    traits.set_is_assumed_state(true);
    traits.set_supports_position(true);
    return traits;
  }

  void control(const cover::CoverCall &call) override;

 private:
   ARCBridgeComponent *parent_{nullptr};
   std::string blind_id_;
   std::string name_;
   bool ignore_control_{true};
+  bool invert_position_{false};
   float last_published_position_{NAN};
 };

}  // namespace arc_bridge
}  // namespace esphome


#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace esphome {
namespace arc_bridge {

class ARCCover;  // forward declaration

class ARCBridgeComponent : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;

  // registration
  void register_cover(const std::string &id, ARCCover *cover);

  // command API
  void send_open(const std::string &id);
  void send_close(const std::string &id);
  void send_stop(const std::string &id);
  void send_move(const std::string &id, uint8_t percent);
  void send_query(const std::string &id);
  void send_pair_command();

  // sensor mapping (optional)
  void map_lq_sensor(const std::string &id, sensor::Sensor *s);
  void map_status_sensor(const std::string &id, text_sensor::TextSensor *s);

  void set_auto_poll_enabled(bool enabled) { this->auto_poll_enabled_ = enabled; }
  void set_auto_poll_interval(uint32_t interval_ms) { this->query_interval_ms_ = interval_ms; }

  bool is_startup_guard_cleared() const { return this->startup_guard_cleared_; }
  // Public helper to send a simple ARC command
  void send_simple(const std::string &id, char cmd, const std::string &arg) {
    this->send_simple_(id, cmd, arg);
  }
  
 protected:
  void handle_frame(const std::string &frame);
  void parse_frame(const std::string &frame);
  void send_simple_(const std::string &id, char command, const std::string &payload = "");

  std::deque<char> rx_buffer_;
  uint32_t boot_millis_{0};
  uint32_t last_query_millis_{0};
  uint32_t last_rx_millis_{0};
  size_t query_index_{0};
  bool startup_guard_cleared_{false};
  bool auto_poll_enabled_{true};
  uint32_t query_interval_ms_{QUERY_INTERVAL_MS};

  std::vector<ARCCover *> covers_;
  std::unordered_map<std::string, sensor::Sensor *> lq_map_;
  std::unordered_map<std::string, text_sensor::TextSensor *> status_map_;

  static const uint32_t QUERY_INTERVAL_MS = 10000;  // 10s per blind
  static const uint32_t STARTUP_GUARD_MS = 10000;   // 10s before accepting control
};

}  // namespace arc_bridge
}  // namespace esphome

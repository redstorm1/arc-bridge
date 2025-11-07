#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <string>
#include <vector>
#include <unordered_map>

namespace esphome {
namespace arc_bridge {

class ARCBlind;

/**
 * ARCBridgeComponent
 * - Owns the UART link and parses ARC frames.
 * - Manages ARCBlind (cover) entities.
 * - Publishes per-blind link quality (sensor) and status (text sensor).
 * - Performs round-robin periodic position queries (!IDr?;).
 */
class ARCBridgeComponent : public Component, public uart::UARTDevice {
 public:
  // lifecycle
  void setup() override;
  void loop() override;

  // register entities
  void add_blind(ARCBlind *blind);
  ARCBlind *find_blind_by_id(const std::string &id);

  // optional per-blind sensors
  void map_lq_sensor(const std::string &id, sensor::Sensor *s);
  void map_status_sensor(const std::string &id, text_sensor::TextSensor *s);

  // commands
  void send_move_command(const std::string &blind_id, uint8_t percent);  // !IDmNNN;
  void send_open_command(const std::string &blind_id);                    // !IDo;
  void send_close_command(const std::string &blind_id);                   // !IDc;
  void send_stop_command(const std::string &blind_id);                    // !IDs;
  void send_position_query(const std::string &blind_id);                  // !IDr?;
  void request_position_now(const std::string &blind_id);

 protected:
  // helpers
  void send_simple_command_(const std::string &blind_id, char command,
                            const std::string &payload = std::string());
  void handle_incoming_frame(const std::string &frame);

  // parsing helpers
  void publish_link_quality_(const std::string &id, int r_raw_hex);
  void publish_status_(const std::string &id, bool have_enp, bool have_enl,
                       int enp_val, int enl_val);
  void publish_position_if_known_(const std::string &id, int pos);

  // timers / state
  uint32_t boot_millis_{0};
  bool startup_guard_cleared_{false};

  std::string rx_buffer_;
  uint32_t last_query_millis_{0};
  size_t query_index_{0};

  static constexpr uint32_t QUERY_INTERVAL_MS{1500};
  static constexpr uint32_t STARTUP_GUARD_MS{10 * 1000};

  // registry
  std::vector<ARCBlind *> blinds_;
  std::unordered_map<std::string, sensor::Sensor *> lq_map_;
  std::unordered_map<std::string, text_sensor::TextSensor *> status_map_;
};

/**
 * ARCBlind
 * - One HA cover per physical blind.
 * - device 0=open, 100=closed ↔ HA 1=open, 0=closed (invert optional)
 */
class ARCBlind : public cover::Cover, public Component {
 public:
  ARCBlind() = default;
  explicit ARCBlind(const std::string &blind_id) : blind_id_(blind_id) {}

  // legacy shims for YAML/codegen compatibility
  //void set_component_source(const std::string &) {}
  void set_component_source(const void *) {}   // accept LOG_STR() safely
  void set_blind_id(const std::string &id) { blind_id_ = id; }

  // lifecycle
  void setup();  // (no override — Cover has no setup())

  // traits
  cover::CoverTraits get_traits() {
    cover::CoverTraits t;
    t.set_supports_position(true);
    t.set_supports_tilt(false);
    return t;
  }

  // state publication
  void publish_position(float position);      // 0..1 to HA
  void publish_raw_position(int device_pos);  // 0..100 device reading

  // identity / linkage
  const std::string &get_blind_id() const { return blind_id_; }
  void set_parent(ARCBridgeComponent *p) { parent_ = p; }
  void set_name(const std::string &name);

  // control
  void control(const cover::CoverCall &call) override;

  // startup guard / config
  void clear_startup_guard();
  void set_invert_position(bool invert);

 protected:
  ARCBridgeComponent *parent_{nullptr};
  std::string blind_id_;
  std::string name_;

  bool invert_position_{false};
  bool ignore_control_{true};  // ignore controls until first valid position arrives
  float last_published_position_{NAN};
};

}  // namespace arc_bridge
}  // namespace esphome

#pragma once
#include "esphome.h"
#include <map>

namespace esphome {
namespace arc_bridge {

class ARCBridgeComponent;

// =============== ARCBlind =================
class ARCBlind : public Component, public cover::Cover {
 public:
  void set_blind_id(const std::string &id) { blind_id_ = id; }
  void set_name(const std::string &name) { name_ = name; }

  // Cover traits
  cover::CoverTraits get_traits() override {
    cover::CoverTraits t;
    t.set_supports_stop(true);
    t.set_supports_position(true);
    t.set_is_assumed_state(false);
    return t;
  }

  void control(const cover::CoverCall &call) override;
  void publish_pos(float p01);

  void set_bridge(ARCBridgeComponent *b) { bridge_ = b; }

  // --- Status tracking ---
  void update_status(bool paired, bool online);
  bool paired() const { return paired_; }
  bool online() const { return online_; }
  void mark_seen() { last_seen_ms_ = millis(); }
  bool timeout(uint32_t now, uint32_t ms) const { return (now - last_seen_ms_) > ms; }

  void set_status_sensor(text_sensor::TextSensor *s) { status_sensor_ = s; }

 protected:
  std::string blind_id_;
  ARCBridgeComponent *bridge_{nullptr};
  bool paired_{true};
  bool online_{true};
  uint32_t last_seen_ms_{0};
  text_sensor::TextSensor *status_sensor_{nullptr};
};

// =============== ARCBridge =================
class ARCBridgeComponent : public Component, public UARTDevice {
 public:
  void setup() override;
  void loop() override;

  void add_blind(ARCBlind *b);
  void map_lq_sensor(const std::string &blind, sensor::Sensor *s);
  void map_status_sensor(const std::string &blind, text_sensor::TextSensor *s);

  void send_cmd(const std::string &ascii);
  void send_move_to_percent(const std::string &blind, int pct);

  void on_pos(const std::string &blind, float p01);
  void on_lq(const std::string &blind, int qpct);
  void on_status(const std::string &blind, const std::string &state);

 protected:
  std::string buf_;
  uint32_t last_rx_{0};

  std::map<std::string, ARCBlind*> blinds_;
  std::map<std::string, sensor::Sensor*> lq_sensors_;
  std::map<std::string, text_sensor::TextSensor*> status_sensors_;

  void parse_frame_(const std::string &f);
  void parse_rf_quality_(const std::string &blind, const std::string &frame);
};

}  // namespace arc_bridge
}  // namespace esphome

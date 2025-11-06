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

  void setup() override {}
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 private:
  struct BlindState {
    ARCBlind *blind{nullptr};
    sensor::Sensor *link_quality_sensor{nullptr};
    text_sensor::TextSensor *status_sensor{nullptr};
    std::string status;
    uint32_t last_query_ms{0};
    uint32_t last_response_ms{0};
    bool pending_query{false};
  };

  BlindState *find_state_(const std::string &blind_id);
  void send_simple_command_(const std::string &blind_id, char command,
                            const std::string &payload = std::string());
  void handle_incoming_byte_(char byte);
  void process_frame_(const std::string &frame);
  void handle_status_update_(BlindState &state, const std::string &blind_id,
                             const std::string &status);
  void handle_link_quality_(BlindState &state, float value);
  void schedule_queries_();
  void check_timeouts_();

  std::vector<ARCBlind *> blinds_;
  std::map<std::string, BlindState> blind_states_;
  std::string rx_buffer_;
  uint32_t last_poll_ms_{0};
};

// --------------------------------------------------------------------
// Individual ARC Blind entity (acts as a Cover)
// --------------------------------------------------------------------
class ARCBlind : public cover::Cover, public Component {
 public:
  void set_blind_id(const std::string &id) { blind_id_ = id; }
  void set_name(const std::string &name) { name_ = name; }
  void set_parent(ARCBridgeComponent *parent) { parent_ = parent; }
  const std::string &get_blind_id() const { return blind_id_; }
  void handle_position_report(uint8_t arc_percent);

 protected:
    cover::CoverTraits get_traits() override {
      cover::CoverTraits traits{};
      traits.set_is_assumed_state(false);
      traits.set_supports_position(true);
      return traits;
    }

  void control(const cover::CoverCall &call) override;

 private:
  ARCBridgeComponent *parent_{nullptr};
  std::string blind_id_;
  std::string name_;
};

}  // namespace arc_bridge
}  // namespace esphome


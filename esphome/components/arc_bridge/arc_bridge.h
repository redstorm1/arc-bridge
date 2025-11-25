#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/cover/cover.h"

namespace esphome {
namespace arc_bridge {

class ARCBlind;

class ARCBridgeComponent : public uart::UARTDevice, public Component {
 public:
  void setup() override;
  void loop() override;

  void register_blind(ARCBlind *blind);

  // Movement callback → used to inhibit polling
  void blind_moved();

  void queue_command(const std::string &cmd);
  void send_position_query(const std::string &id);

 protected:
  void poll_all_positions();
  void retry_unanswered_positions();
  void process_incoming_bytes();
  void handle_packet(const std::string &pkt);

  std::vector<ARCBlind *> blinds_;
  std::string rx_buffer_;

  uint32_t boot_millis_{0};
  uint32_t last_poll_millis_{0};
  uint32_t last_move_millis_{0};

  bool startup_guard_cleared_{false};
  bool move_inhibit_active_{false};
};

class ARCBlind : public cover::Cover {
 public:
  std::string blind_id_;
  std::string name_;

  // State tracking for polling system
  bool awaiting_reply_{false};
  uint32_t last_query_time_{0};

  // Called by ARCBridgeComponent when parsing packet
  void set_position_from_motor(int pct) {
    this->publish_state(1.0f - (pct / 100.0f));  // convert ARC to ESPHome
  }
};

}  // namespace arc_bridge
}  // namespace esphome

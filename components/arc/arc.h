\
#pragma once
#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/api/custom_api_device.h"
#include <map>
#include <deque>

namespace esphome {
namespace arc {

// A discovered device on the ARC bus
struct ARCDeviceInfo {
  std::string addr;     // 3-char ASCII address or any ASCII id used by your fleet
  std::string version;  // e.g., "A21"
  uint32_t last_seen_ms{0};
  int last_pos{-1};     // 0-100
  int last_tilt{-1};    // 0-180 (degrees)
};

// Forward declaration
class ARCComponent;

// A single cover mapped to an ARC address
class ARCCover : public cover::Cover, public Parented<ARCComponent> {
 public:
  void set_address(const std::string &addr) { address_ = addr; }
  const std::string &get_address() const { return address_; }

  // cover::Cover overrides
  cover::CoverTraits get_traits() override;
 protected:
  void control(const cover::CoverCall &call) override;
  void dump_config() override;
  std::string address_;
};

// Main ARC bus component (UART + protocol)
class ARCComponent : public Component, public uart::UARTDevice, public api::CustomAPIDevice {
 public:
  ARCComponent() = default;

  // Settings
  void set_discovery_on_boot(bool v) { discovery_on_boot_ = v; }
  void set_broadcast_interval_ms(uint32_t v) { broadcast_interval_ms_ = v; }
  void set_idle_gap_ms(uint32_t v) { idle_gap_ms_ = v; }

  // Component lifecycle
  void setup() override;
  void loop() override;
  void dump_config() override;

  // API services
  void start_discovery();
  void stop_discovery();
  void query_all();  // send !000V?;

  // Cover helpers for ARCCover
  void send_open(const std::string &addr);
  void send_close(const std::string &addr);
  void send_stop(const std::string &addr);
  void send_move_pct(const std::string &addr, int pct);  // 0..100
  void send_tilt_deg(const std::string &addr, int deg);  // 0..180

  // Device registry
  const std::map<std::string, ARCDeviceInfo> &devices() const { return devices_; }
  void register_cover(ARCCover *c);

  // Bus utilities
  bool bus_idle() const;

 protected:
  // RX frame parsing
  void on_byte(uint8_t b);
  void parse_frame_(const std::string &frame);
  bool valid_frame_(const std::string &frame) const;

  // Send helpers
  void send_cmd_(const std::string &addr, char cmd, const std::string &data = std::string());
  void send_raw_(const std::string &s);

  // State
  std::string rx_buf_;
  uint32_t last_byte_ms_{0};
  uint32_t last_tx_ms_{0};
  bool discovery_{false};
  bool discovery_on_boot_{false};
  uint32_t broadcast_interval_ms_{5000};
  uint32_t last_broadcast_ms_{0};
  uint32_t idle_gap_ms_{30};

  std::map<std::string, ARCDeviceInfo> devices_;
  std::vector<ARCCover*> covers_;
};

}  // namespace arc
}  // namespace esphome

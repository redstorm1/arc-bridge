#pragma once

#include <string>

#if __has_include(<optional>)
#include <optional>
namespace esphome_arc_bridge_std_optional = std;
#elif __has_include(<experimental/optional>)
#include <experimental/optional>
namespace esphome_arc_bridge_std_optional = std::experimental;
#else
#error "An optional implementation is required to build protocol.h"
#endif

namespace esphome {
namespace arc_bridge {

struct ParsedFrame {
  bool valid{false};
  std::string id;
  std::string reply_token;
  bool address_ack{false};

  esphome_arc_bridge_std_optional::optional<int> position_percent;
  bool position_in_motion{false};
  esphome_arc_bridge_std_optional::optional<int> tilt_degrees;
  esphome_arc_bridge_std_optional::optional<int> rssi_raw;

  bool lost_link{false};
  bool not_paired{false};
  bool no_position{false};

  esphome_arc_bridge_std_optional::optional<int> voltage_centivolts;
  esphome_arc_bridge_std_optional::optional<int> speed_rpm;

  esphome_arc_bridge_std_optional::optional<std::string> version_code;
  esphome_arc_bridge_std_optional::optional<char> motor_type_code;
  esphome_arc_bridge_std_optional::optional<int> version_major;
  esphome_arc_bridge_std_optional::optional<int> version_minor;

  esphome_arc_bridge_std_optional::optional<std::string> limits_code;
  esphome_arc_bridge_std_optional::optional<std::string> error_code;
};

ParsedFrame parse_arc_frame(const std::string &frame);

}  // namespace arc_bridge
}  // namespace esphome

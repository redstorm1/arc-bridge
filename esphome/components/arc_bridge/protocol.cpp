#include "protocol.h"

#include <cctype>
#include <cstdlib>

namespace esphome {
namespace arc_bridge {

namespace {

esphome_arc_bridge_std_optional::optional<int> parse_decimal_after_(const std::string &text, size_t start) {
  size_t end = start;
  while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
    end++;
  }
  if (end == start) {
    return esphome_arc_bridge_std_optional::nullopt;
  }
  return std::atoi(text.substr(start, end - start).c_str());
}

esphome_arc_bridge_std_optional::optional<int> parse_hex_byte_after_(const std::string &text, size_t start) {
  if (start + 2 > text.size()) {
    return esphome_arc_bridge_std_optional::nullopt;
  }
  const char hi = text[start];
  const char lo = text[start + 1];
  if (!std::isxdigit(static_cast<unsigned char>(hi)) ||
      !std::isxdigit(static_cast<unsigned char>(lo))) {
    return esphome_arc_bridge_std_optional::nullopt;
  }
  return std::strtol(text.substr(start, 2).c_str(), nullptr, 16);
}

}  // namespace

ParsedFrame parse_arc_frame(const std::string &frame) {
  ParsedFrame parsed;

  if (frame.size() < 5 || frame.front() != '!' || frame.back() != ';') {
    return parsed;
  }

  const std::string body = frame.substr(1, frame.size() - 2);
  if (body.size() < 3) {
    return parsed;
  }

  parsed.id = body.substr(0, 3);
  const std::string rest = body.substr(3);
  parsed.valid = true;
  const size_t reply_end = rest.find(',');
  parsed.reply_token = rest.substr(0, reply_end == std::string::npos ? rest.size() : reply_end);

  parsed.address_ack = parsed.reply_token == "A";
  parsed.lost_link = parsed.reply_token == "Enl" || rest.find("Enl") != std::string::npos;
  parsed.not_paired = parsed.reply_token == "Enp" || rest.find("Enp") != std::string::npos;
  parsed.no_position = parsed.reply_token == "U";
  if (!parsed.lost_link && !parsed.not_paired && parsed.reply_token.size() == 3 &&
      parsed.reply_token.front() == 'E') {
    parsed.error_code = parsed.reply_token.substr(1);
  }

  size_t pvc_pos = rest.find("pVc");
  if (pvc_pos != std::string::npos) {
    parsed.voltage_centivolts = parse_decimal_after_(rest, pvc_pos + 3);
  }

  size_t speed_pos = rest.find("pSc");
  if (speed_pos != std::string::npos) {
    parsed.speed_rpm = parse_decimal_after_(rest, speed_pos + 3);
  }

  size_t limits_pos = rest.find("pP");
  if (limits_pos != std::string::npos && limits_pos + 4 <= rest.size()) {
    const std::string code = rest.substr(limits_pos + 2, 2);
    if (std::isalnum(static_cast<unsigned char>(code[0])) &&
        std::isalnum(static_cast<unsigned char>(code[1]))) {
      parsed.limits_code = code;
    }
  }

  size_t version_pos = rest.find('v');
  if (version_pos != std::string::npos && version_pos + 2 < rest.size()) {
    const char type_code = rest[version_pos + 1];
    if (std::isalpha(static_cast<unsigned char>(type_code))) {
      size_t digit_start = version_pos + 2;
      size_t digit_end = digit_start;
      while (digit_end < rest.size() &&
             std::isdigit(static_cast<unsigned char>(rest[digit_end]))) {
        digit_end++;
      }
      if (digit_end > digit_start) {
        parsed.motor_type_code = type_code;
        parsed.version_code = rest.substr(version_pos + 1, digit_end - (version_pos + 1));
        if (digit_end - digit_start >= 2) {
          parsed.version_major = rest[digit_start] - '0';
          parsed.version_minor = rest[digit_start + 1] - '0';
        }
      }
    }
  }

  size_t move_pos = rest.find('<');
  if (move_pos != std::string::npos) {
    parsed.position_percent = parse_decimal_after_(rest, move_pos + 1);
    parsed.position_in_motion = static_cast<bool>(parsed.position_percent);
  }

  if (!parsed.position_percent) {
    size_t pos = rest.find('r');
    if (pos != std::string::npos) {
      parsed.position_percent = parse_decimal_after_(rest, pos + 1);
    } 
  }

  size_t tilt_pos = rest.find('b');
  if (tilt_pos != std::string::npos) {
    parsed.tilt_degrees = parse_decimal_after_(rest, tilt_pos + 1);
  }

  size_t rssi_pos = rest.find('R');
  if (rssi_pos != std::string::npos) {
    parsed.rssi_raw = parse_hex_byte_after_(rest, rssi_pos + 1);
  }

  return parsed;
}

}  // namespace arc_bridge
}  // namespace esphome

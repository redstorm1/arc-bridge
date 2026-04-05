#include "protocol.h"

#include <cstdlib>
#include <iostream>
#include <string>

using esphome::arc_bridge::ParsedFrame;
using esphome::arc_bridge::parse_arc_frame;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
  }
}

void test_position_and_rssi() {
  const ParsedFrame frame = parse_arc_frame("!USZr100b180,RA6;");
  require(frame.valid, "position/RSSI frame should parse");
  require(frame.id == "USZ", "position/RSSI frame should preserve the blind id");
  require(frame.reply_token == "r100b180",
          "position/RSSI frame should preserve the leading reply token");
  require(static_cast<bool>(frame.position_percent) && *frame.position_percent == 100,
          "position/RSSI frame should extract the final position");
  require(static_cast<bool>(frame.tilt_degrees) && *frame.tilt_degrees == 180,
          "position/RSSI frame should extract the tilt");
  require(static_cast<bool>(frame.rssi_raw) && *frame.rssi_raw == 0xA6,
          "position/RSSI frame should extract the RSSI byte");
  require(!frame.position_in_motion, "final position frame should not be marked moving");
}

void test_in_motion_position() {
  const ParsedFrame frame = parse_arc_frame("!USZ<09b00;");
  require(frame.valid, "in-motion frame should parse");
  require(frame.reply_token == "<09b00",
          "in-motion frame should preserve the leading reply token");
  require(static_cast<bool>(frame.position_percent) && *frame.position_percent == 9,
          "in-motion frame should extract the current position");
  require(frame.position_in_motion, "in-motion frame should be marked moving");
}

void test_motion_echo_tokens() {
  const ParsedFrame open_echo = parse_arc_frame("!QJ0o,R98;");
  require(open_echo.valid && open_echo.reply_token == "o",
          "open echo should preserve the immediate reply token");

  const ParsedFrame stop_echo = parse_arc_frame("!QJ0s,R9D;");
  require(stop_echo.valid && stop_echo.reply_token == "s",
          "stop echo should preserve the immediate reply token");

  const ParsedFrame jog_echo = parse_arc_frame("!QJ0oA,R9C;");
  require(jog_echo.valid && jog_echo.reply_token == "oA",
          "jog-open echo should preserve the immediate reply token");

  const ParsedFrame move_echo = parse_arc_frame("!QJ0m050,R9C;");
  require(move_echo.valid && move_echo.reply_token == "m050",
          "move echo should preserve the immediate reply token");
}

void test_unavailable_and_pairing_states() {
  const ParsedFrame lost_link = parse_arc_frame("!USZEnl;");
  require(lost_link.valid && lost_link.lost_link, "Enl should map to lost link");

  const ParsedFrame not_paired = parse_arc_frame("!USZEnp;");
  require(not_paired.valid && not_paired.not_paired, "Enp should map to not paired");

  const ParsedFrame address_ack = parse_arc_frame("!USZA;");
  require(address_ack.valid && address_ack.address_ack, "A should map to an address acknowledgement");

  const ParsedFrame generic_error = parse_arc_frame("!USZEdf;");
  require(generic_error.valid && static_cast<bool>(generic_error.error_code) &&
              *generic_error.error_code == "df",
          "generic Exx should extract the error code");
  require(!generic_error.lost_link && !generic_error.not_paired,
          "generic Exx errors should not be remapped to Enl/Enp states");

  const ParsedFrame no_position = parse_arc_frame("!USZU;");
  require(no_position.valid && no_position.no_position, "U should map to no-position feedback");
}

void test_extended_queries() {
  const ParsedFrame voltage = parse_arc_frame("!USZpVc123;");
  require(static_cast<bool>(voltage.voltage_centivolts) && *voltage.voltage_centivolts == 123,
          "pVc should extract the voltage payload");

  const ParsedFrame speed = parse_arc_frame("!USZpSc028;");
  require(static_cast<bool>(speed.speed_rpm) && *speed.speed_rpm == 28,
          "pSc should extract the speed payload");

  const ParsedFrame version = parse_arc_frame("!USZvA21;");
  require(static_cast<bool>(version.version_code) && *version.version_code == "A21",
          "version should extract the raw motor version");
  require(static_cast<bool>(version.motor_type_code) && *version.motor_type_code == 'A',
          "version should extract the motor type");
  require(static_cast<bool>(version.version_major) && *version.version_major == 2,
          "version should extract the major version");
  require(static_cast<bool>(version.version_minor) && *version.version_minor == 1,
          "version should extract the minor version");

  const ParsedFrame limits = parse_arc_frame("!USZpP03;");
  require(static_cast<bool>(limits.limits_code) && *limits.limits_code == "03",
          "pP should extract the limits code");
}

}  // namespace

int main() {
  test_position_and_rssi();
  test_in_motion_position();
  test_motion_echo_tokens();
  test_unavailable_and_pairing_states();
  test_extended_queries();
  std::cout << "protocol parser tests passed" << std::endl;
  return 0;
}

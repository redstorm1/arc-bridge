#include "pairing.h"

#include <cstdlib>
#include <iostream>
#include <string>

using esphome::arc_bridge::PairingOutcome;
using esphome::arc_bridge::PairingOutcomeType;
using esphome::arc_bridge::PairingSession;
using esphome::arc_bridge::ParsedFrame;
using esphome::arc_bridge::build_pair_command_frame;
using esphome::arc_bridge::check_pairing_timeout;
using esphome::arc_bridge::describe_error_code;
using esphome::arc_bridge::handle_pairing_frame;
using esphome::arc_bridge::parse_arc_frame;
using esphome::arc_bridge::start_pairing_session;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
  }
}

void test_pair_frame_building() {
  require(build_pair_command_frame() == "!000&;",
          "random pairing should use the global pairing frame");
}

void test_random_pairing_success() {
  PairingSession session;
  start_pairing_session(session, 100);

  const PairingOutcome outcome = handle_pairing_frame(session, parse_arc_frame("!QJ0A;"));
  require(outcome.type == PairingOutcomeType::SUCCESS,
          "address acknowledgement during pairing should be treated as success");
  require(outcome.message == "Paired", "pairing success should produce the Paired status");
  require(outcome.paired_id == "QJ0", "pairing success should expose the paired id");
  require(!session.active, "pairing session should close after success");
}

void test_pairing_timeout_and_unsolicited_ack() {
  PairingSession session;
  start_pairing_session(session, 100);
  const PairingOutcome timeout = check_pairing_timeout(session, 30100, 30000);
  require(timeout.type == PairingOutcomeType::TIMEOUT,
          "expired pairing sessions should time out");
  require(timeout.message == "Timed Out", "pairing timeout should produce the Timed Out status");
  require(!session.active, "timed out pairing session should close");

  PairingSession idle_session;
  const PairingOutcome ack = handle_pairing_frame(idle_session, parse_arc_frame("!USZA;"));
  require(ack.type == PairingOutcomeType::GENERIC_ACK,
          "unsolicited A replies should stay generic outside pairing");
  require(ack.paired_id == "USZ", "generic ack should preserve the responding id");
}

void test_pairing_error_mapping() {
  PairingSession session;
  start_pairing_session(session, 100);
  const PairingOutcome error = handle_pairing_frame(session, parse_arc_frame("!QJ0Edf;"));
  require(error.type == PairingOutcomeType::ERROR,
          "generic Exx during pairing should fail the session");
  require(error.message == "Error: Hub motor limit exceeded",
          "pairing errors should use decoded protocol text");
  require(!session.active, "pairing error should close the session");

  require(describe_error_code("np") == "Invalid motor address",
          "known error codes should decode to friendly text");
}

}  // namespace

int main() {
  test_pair_frame_building();
  test_random_pairing_success();
  test_pairing_timeout_and_unsolicited_ack();
  test_pairing_error_mapping();
  std::cout << "pairing tests passed" << std::endl;
  return 0;
}

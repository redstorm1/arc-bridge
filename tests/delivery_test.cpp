#include "delivery.h"

#include <cstdlib>
#include <iostream>
#include <string>

using esphome::arc_bridge::DeliveryExpectation;
using esphome::arc_bridge::DeliveryTimeoutAction;
using esphome::arc_bridge::ParsedFrame;
using esphome::arc_bridge::PendingDeliveryPolicy;
using esphome::arc_bridge::frame_confirms_delivery;
using esphome::arc_bridge::next_delivery_timeout_action;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
  }
}

void test_position_feedback_matching() {
  ParsedFrame moving;
  moving.valid = true;
  moving.id = "QJ0";
  moving.position_percent = 9;
  moving.position_in_motion = true;
  require(frame_confirms_delivery(moving, "QJ0", DeliveryExpectation::BLIND_REPLY),
          "position feedback should confirm a pending motion command");

  ParsedFrame finished;
  finished.valid = true;
  finished.id = "QJ0";
  finished.position_percent = 50;
  require(frame_confirms_delivery(finished, "QJ0", DeliveryExpectation::BLIND_REPLY),
          "final position feedback should confirm a pending motion command");

  ParsedFrame unavailable;
  unavailable.valid = true;
  unavailable.id = "QJ0";
  unavailable.no_position = true;
  require(frame_confirms_delivery(unavailable, "QJ0", DeliveryExpectation::BLIND_REPLY),
          "U feedback should still confirm that the blind answered");

  ParsedFrame lost_link;
  lost_link.valid = true;
  lost_link.id = "QJ0";
  lost_link.lost_link = true;
  require(frame_confirms_delivery(lost_link, "QJ0", DeliveryExpectation::BLIND_REPLY),
          "lost-link feedback should terminate delivery tracking");

  ParsedFrame static_reply;
  static_reply.valid = true;
  static_reply.id = "QJ0";
  static_reply.voltage_centivolts = 123;
  static_reply.reply_token = "pVc123";
  require(!frame_confirms_delivery(static_reply, "QJ0", DeliveryExpectation::BLIND_REPLY),
          "static telemetry alone should not confirm a motion command");
}

void test_echo_ack_matching() {
  ParsedFrame open_echo;
  open_echo.valid = true;
  open_echo.id = "QJ0";
  open_echo.reply_token = "o";
  require(frame_confirms_delivery(open_echo, "QJ0", DeliveryExpectation::BLIND_REPLY, "o"),
          "open echo should confirm an open command");

  ParsedFrame stop_echo;
  stop_echo.valid = true;
  stop_echo.id = "QJ0";
  stop_echo.reply_token = "s";
  require(frame_confirms_delivery(stop_echo, "QJ0", DeliveryExpectation::BLIND_REPLY, "s"),
          "stop echo should confirm a stop command");

  ParsedFrame close_echo;
  close_echo.valid = true;
  close_echo.id = "QJ0";
  close_echo.reply_token = "c";
  require(frame_confirms_delivery(close_echo, "QJ0", DeliveryExpectation::BLIND_REPLY, "c"),
          "close echo should confirm a close command");

  ParsedFrame favorite_echo;
  favorite_echo.valid = true;
  favorite_echo.id = "QJ0";
  favorite_echo.reply_token = "f";
  require(frame_confirms_delivery(favorite_echo, "QJ0", DeliveryExpectation::BLIND_REPLY, "f"),
          "favorite echo should confirm a favorite command");

  ParsedFrame jog_echo;
  jog_echo.valid = true;
  jog_echo.id = "QJ0";
  jog_echo.reply_token = "oA";
  require(frame_confirms_delivery(jog_echo, "QJ0", DeliveryExpectation::BLIND_REPLY, "oA"),
          "jog echo should confirm a jog command");

  ParsedFrame move_echo_exact;
  move_echo_exact.valid = true;
  move_echo_exact.id = "QJ0";
  move_echo_exact.reply_token = "m050";
  require(frame_confirms_delivery(move_echo_exact, "QJ0", DeliveryExpectation::BLIND_REPLY,
                                  "m050", "m"),
          "exact move echo should confirm a move command");

  ParsedFrame move_echo_fallback;
  move_echo_fallback.valid = true;
  move_echo_fallback.id = "QJ0";
  move_echo_fallback.reply_token = "m";
  require(frame_confirms_delivery(move_echo_fallback, "QJ0", DeliveryExpectation::BLIND_REPLY,
                                  "m050", "m"),
          "opcode-only move echo should still confirm a move command");
}

void test_wrong_blind_does_not_match() {
  ParsedFrame open_echo;
  open_echo.valid = true;
  open_echo.id = "USZ";
  open_echo.reply_token = "o";
  require(!frame_confirms_delivery(open_echo, "QJ0", DeliveryExpectation::BLIND_REPLY, "o"),
          "a reply from another blind should not confirm delivery");
}

void test_timeout_action_progression() {
  PendingDeliveryPolicy policy;
  policy.retry_limit = 1;
  policy.timeout_ms = 1500;
  policy.allow_retry = true;
  policy.last_activity_ms = 100;

  require(next_delivery_timeout_action(policy, 1200) == DeliveryTimeoutAction::NONE,
          "timeouts should stay idle before the configured threshold");
  require(next_delivery_timeout_action(policy, 1700) == DeliveryTimeoutAction::SEND_VERIFY_QUERY,
          "first timeout should trigger a verification query");

  policy.verification_sent = true;
  policy.last_activity_ms = 1700;
  require(next_delivery_timeout_action(policy, 3300) == DeliveryTimeoutAction::RETRY_COMMAND,
          "second timeout should retry when retries remain");

  policy.retries_used = 1;
  policy.last_activity_ms = 3300;
  require(next_delivery_timeout_action(policy, 4900) == DeliveryTimeoutAction::GIVE_UP,
          "timeouts should give up after the retry limit is reached");
}

void test_verify_only_flow() {
  PendingDeliveryPolicy policy;
  policy.retry_limit = 1;
  policy.timeout_ms = 1500;
  policy.allow_retry = false;
  policy.last_activity_ms = 100;

  require(next_delivery_timeout_action(policy, 1700) == DeliveryTimeoutAction::SEND_VERIFY_QUERY,
          "verify-only commands should still probe for a reply once");

  policy.verification_sent = true;
  policy.last_activity_ms = 1700;
  require(next_delivery_timeout_action(policy, 3300) == DeliveryTimeoutAction::GIVE_UP,
          "verify-only commands should stop instead of replaying the motion");
}

}  // namespace

int main() {
  test_position_feedback_matching();
  test_echo_ack_matching();
  test_wrong_blind_does_not_match();
  test_timeout_action_progression();
  test_verify_only_flow();
  std::cout << "delivery tests passed" << std::endl;
  return 0;
}

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
  moving.position_percent = 9;
  moving.position_in_motion = true;
  require(frame_confirms_delivery(moving, DeliveryExpectation::POSITION_FEEDBACK),
          "position feedback should confirm a pending motion command");

  ParsedFrame finished;
  finished.valid = true;
  finished.position_percent = 50;
  require(frame_confirms_delivery(finished, DeliveryExpectation::POSITION_FEEDBACK),
          "final position feedback should confirm a pending motion command");

  ParsedFrame unavailable;
  unavailable.valid = true;
  unavailable.no_position = true;
  require(frame_confirms_delivery(unavailable, DeliveryExpectation::POSITION_FEEDBACK),
          "U feedback should still confirm that the blind answered");

  ParsedFrame lost_link;
  lost_link.valid = true;
  lost_link.lost_link = true;
  require(frame_confirms_delivery(lost_link, DeliveryExpectation::POSITION_FEEDBACK),
          "lost-link feedback should terminate delivery tracking");

  ParsedFrame static_reply;
  static_reply.valid = true;
  static_reply.voltage_centivolts = 123;
  require(!frame_confirms_delivery(static_reply, DeliveryExpectation::POSITION_FEEDBACK),
          "static telemetry alone should not confirm a motion command");
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
  test_timeout_action_progression();
  test_verify_only_flow();
  std::cout << "delivery tests passed" << std::endl;
  return 0;
}

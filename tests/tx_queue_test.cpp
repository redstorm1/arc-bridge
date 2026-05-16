#include "tx_queue.h"

#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>

using esphome::arc_bridge::TxPacingClass;
using esphome::arc_bridge::TxQueueItem;
using esphome::arc_bridge::drop_pending_poll_items;
using esphome::arc_bridge::tx_item_can_send_while_delivery_pending;
using esphome::arc_bridge::tx_gap_ms_for;

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
  }
}

void test_gap_mapping() {
  require(tx_gap_ms_for(TxPacingClass::STANDARD) == 800,
          "default queue items should keep the 800 ms gap");
  require(tx_gap_ms_for(TxPacingClass::MOTION) == 200,
          "motion queue items should use the 200 ms gap");
  require(tx_gap_ms_for(TxPacingClass::MOTION, 350) == 350,
          "motion queue items should honor an overridden motion gap");
  require(tx_gap_ms_for(TxPacingClass::STANDARD, 350) == 800,
          "standard queue items should ignore the overridden motion gap");
}

void test_drop_pending_polls_removes_only_poll_items() {
  std::deque<TxQueueItem> queue = {
      {"!USZr?;", TxPacingClass::STANDARD, true, "", esphome::arc_bridge::DeliveryExpectation::NONE,
       false, 0, "", ""},
      {"!USZm050;", TxPacingClass::MOTION, false, "",
       esphome::arc_bridge::DeliveryExpectation::NONE, false, 0, "", ""},
      {"!USZpVc?;", TxPacingClass::STANDARD, true, "",
       esphome::arc_bridge::DeliveryExpectation::NONE, false, 0, "", ""},
      {"!000&;", TxPacingClass::STANDARD, false, "", esphome::arc_bridge::DeliveryExpectation::NONE,
       false, 0, "", ""},
  };

  drop_pending_poll_items(queue);

  require(queue.size() == 2, "dropping polls should leave only non-poll items");
  require(queue[0].frame == "!USZm050;", "motion frame should remain after poll drop");
  require(queue[1].frame == "!000&;", "default non-poll frame should remain after poll drop");
}

void test_priority_motion_sits_ahead_of_polls() {
  std::deque<TxQueueItem> queue;
  queue.push_back({"!USZr?;", TxPacingClass::STANDARD, true, "",
                   esphome::arc_bridge::DeliveryExpectation::NONE, false, 0, "", ""});
  queue.push_back({"!USZpVc?;", TxPacingClass::STANDARD, true, "",
                   esphome::arc_bridge::DeliveryExpectation::NONE, false, 0, "", ""});
  queue.push_front({"!USZo;", TxPacingClass::MOTION, false, "",
                    esphome::arc_bridge::DeliveryExpectation::NONE, false, 0, "", ""});

  require(queue.front().frame == "!USZo;",
          "priority motion frame should sit at the front of the queue");

  drop_pending_poll_items(queue);

  require(queue.size() == 1 && queue.front().frame == "!USZo;",
          "dropping pending polls should preserve the priority motion frame");
}

void test_delivery_gating_allows_only_matching_retry_or_untracked_frames() {
  TxQueueItem other_motion{"!USZo;", TxPacingClass::MOTION, false, "USZ",
                           esphome::arc_bridge::DeliveryExpectation::BLIND_REPLY,
                           true, 2, "o", ""};
  require(!tx_item_can_send_while_delivery_pending(other_motion, "QJ0", 1),
          "new motion for another blind should wait while delivery is pending");

  TxQueueItem matching_retry{"!QJ0o;", TxPacingClass::MOTION, false, "QJ0",
                             esphome::arc_bridge::DeliveryExpectation::BLIND_REPLY,
                             true, 1, "o", ""};
  require(tx_item_can_send_while_delivery_pending(matching_retry, "QJ0", 1),
          "matching retry should be allowed through pending-delivery gating");

  TxQueueItem verification_query{"!QJ0r?;", TxPacingClass::STANDARD, false, "",
                                 esphome::arc_bridge::DeliveryExpectation::NONE,
                                 false, 0, "", ""};
  require(tx_item_can_send_while_delivery_pending(verification_query, "QJ0", 1),
          "untracked verification query should be allowed while delivery is pending");

  require(tx_item_can_send_while_delivery_pending(other_motion, "", 0),
          "delivery gating should not block when no delivery is pending");
}

}  // namespace

int main() {
  test_gap_mapping();
  test_drop_pending_polls_removes_only_poll_items();
  test_priority_motion_sits_ahead_of_polls();
  test_delivery_gating_allows_only_matching_retry_or_untracked_frames();
  std::cout << "tx queue tests passed" << std::endl;
  return 0;
}

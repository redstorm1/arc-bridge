#pragma once

#include "delivery.h"

#include <cstdint>
#include <deque>
#include <string>

namespace esphome {
namespace arc_bridge {

static constexpr uint32_t DEFAULT_TX_GAP_MS = 800;
static constexpr uint32_t DEFAULT_MOTION_TX_GAP_MS = 200;

enum class TxPacingClass : uint8_t {
  STANDARD = 0,
  MOTION = 1,
};

struct TxQueueItem {
  std::string frame;
  TxPacingClass pacing_class{TxPacingClass::STANDARD};
  bool is_poll{false};
  std::string blind_id;
  DeliveryExpectation delivery_expectation{DeliveryExpectation::NONE};
  bool allow_retry{false};
  uint32_t tracking_id{0};
  std::string expected_ack_token;
  std::string expected_ack_prefix;
};

uint32_t tx_gap_ms_for(TxPacingClass pacing_class,
                       uint32_t motion_tx_gap_ms = DEFAULT_MOTION_TX_GAP_MS);
void drop_pending_poll_items(std::deque<TxQueueItem> &queue);

}  // namespace arc_bridge
}  // namespace esphome

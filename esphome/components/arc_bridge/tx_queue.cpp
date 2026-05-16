#include "tx_queue.h"

#include <algorithm>

namespace esphome {
namespace arc_bridge {

uint32_t tx_gap_ms_for(TxPacingClass pacing_class, uint32_t motion_tx_gap_ms) {
  switch (pacing_class) {
    case TxPacingClass::MOTION:
      return motion_tx_gap_ms;
    case TxPacingClass::STANDARD:
    default:
      return DEFAULT_TX_GAP_MS;
  }
}

void drop_pending_poll_items(std::deque<TxQueueItem> &queue) {
  queue.erase(
      std::remove_if(queue.begin(), queue.end(),
                     [](const TxQueueItem &item) { return item.is_poll; }),
      queue.end());
}

bool tx_item_can_send_while_delivery_pending(const TxQueueItem &item,
                                             const std::string &pending_blind_id,
                                             uint32_t pending_tracking_id) {
  if (pending_blind_id.empty() || pending_tracking_id == 0) {
    return true;
  }
  if (item.delivery_expectation == DeliveryExpectation::NONE) {
    return true;
  }
  return item.blind_id == pending_blind_id && item.tracking_id == pending_tracking_id;
}

}  // namespace arc_bridge
}  // namespace esphome

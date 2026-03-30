#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace esphome {
namespace arc_bridge {

enum class TxPacingClass : uint8_t {
  STANDARD = 0,
  MOTION = 1,
};

struct TxQueueItem {
  std::string frame;
  TxPacingClass pacing_class{TxPacingClass::STANDARD};
  bool is_poll{false};
};

uint32_t tx_gap_ms_for(TxPacingClass pacing_class);
void drop_pending_poll_items(std::deque<TxQueueItem> &queue);

}  // namespace arc_bridge
}  // namespace esphome

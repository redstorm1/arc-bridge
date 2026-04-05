#pragma once

#include "protocol.h"

#include <cstdint>

namespace esphome {
namespace arc_bridge {

enum class DeliveryExpectation : uint8_t {
  NONE = 0,
  BLIND_REPLY = 1,
};

enum class DeliveryTimeoutAction : uint8_t {
  NONE = 0,
  SEND_VERIFY_QUERY = 1,
  RETRY_COMMAND = 2,
  GIVE_UP = 3,
};

struct PendingDeliveryPolicy {
  uint8_t retries_used{0};
  uint8_t retry_limit{0};
  uint32_t last_activity_ms{0};
  uint32_t timeout_ms{0};
  bool verification_sent{false};
  bool allow_retry{false};
};

bool frame_confirms_delivery(const ParsedFrame &parsed, const std::string &blind_id,
                             DeliveryExpectation expectation,
                             const std::string &expected_ack_token = "",
                             const std::string &expected_ack_prefix = "");
DeliveryTimeoutAction next_delivery_timeout_action(const PendingDeliveryPolicy &policy, uint32_t now_ms);

}  // namespace arc_bridge
}  // namespace esphome

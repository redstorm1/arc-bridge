#include "delivery.h"

namespace esphome {
namespace arc_bridge {

bool frame_confirms_delivery(const ParsedFrame &parsed, DeliveryExpectation expectation) {
  switch (expectation) {
    case DeliveryExpectation::POSITION_FEEDBACK:
      return parsed.lost_link || parsed.not_paired || parsed.no_position ||
             static_cast<bool>(parsed.position_percent);
    case DeliveryExpectation::NONE:
    default:
      return false;
  }
}

DeliveryTimeoutAction next_delivery_timeout_action(const PendingDeliveryPolicy &policy,
                                                   uint32_t now_ms) {
  if (policy.timeout_ms == 0) {
    return DeliveryTimeoutAction::NONE;
  }

  if (now_ms - policy.last_activity_ms < policy.timeout_ms) {
    return DeliveryTimeoutAction::NONE;
  }

  if (!policy.verification_sent) {
    return DeliveryTimeoutAction::SEND_VERIFY_QUERY;
  }

  if (policy.allow_retry && policy.retries_used < policy.retry_limit) {
    return DeliveryTimeoutAction::RETRY_COMMAND;
  }

  return DeliveryTimeoutAction::GIVE_UP;
}

}  // namespace arc_bridge
}  // namespace esphome

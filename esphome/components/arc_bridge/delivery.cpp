#include "delivery.h"

namespace esphome {
namespace arc_bridge {

namespace {

bool matches_prefix_(const std::string &value, const std::string &prefix) {
  return !prefix.empty() && value.rfind(prefix, 0) == 0;
}

}  // namespace

bool frame_confirms_delivery(const ParsedFrame &parsed, const std::string &blind_id,
                             DeliveryExpectation expectation,
                             const std::string &expected_ack_token,
                             const std::string &expected_ack_prefix) {
  if (parsed.id != blind_id) {
    return false;
  }

  switch (expectation) {
    case DeliveryExpectation::BLIND_REPLY:
      if (!expected_ack_token.empty() && parsed.reply_token == expected_ack_token) {
        return true;
      }
      if (matches_prefix_(parsed.reply_token, expected_ack_prefix)) {
        return true;
      }
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

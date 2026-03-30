#include "arc_bridge_group_cover.h"

#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace arc_bridge_group {

static const char *const TAG = "arc_bridge_group";

void ARCBridgeGroupCover::setup() {
  for (auto *member : this->members_) {
    if (member == nullptr) {
      continue;
    }
    member->add_on_state_callback([this]() { this->recompute_state_(); });
  }

  this->recompute_state_();
  this->disable_loop();
}

void ARCBridgeGroupCover::dump_config() {
  LOG_COVER("", "ARC Bridge Group Cover", this);
  ESP_LOGCONFIG(TAG, "  Members: %u", static_cast<unsigned>(this->members_.size()));
}

cover::CoverTraits ARCBridgeGroupCover::get_traits() {
  cover::CoverTraits traits;
  traits.set_is_assumed_state(true);
  traits.set_supports_position(true);
  traits.set_supports_stop(true);
  return traits;
}

void ARCBridgeGroupCover::control(const cover::CoverCall &call) {
  for (auto *member : this->members_) {
    if (member == nullptr) {
      continue;
    }

    auto member_call = member->make_call();
    if (call.get_stop()) {
      member_call.set_stop(true);
    } else if (call.get_position().has_value()) {
      member_call.set_position(*call.get_position());
    } else {
      continue;
    }
    member_call.perform();
  }
}

void ARCBridgeGroupCover::recompute_state_() {
  float position_total = 0.0f;
  size_t positioned_members = 0;
  bool any_opening = false;
  bool any_closing = false;

  for (auto *member : this->members_) {
    if (member == nullptr) {
      continue;
    }

    if (member->current_operation == cover::COVER_OPERATION_OPENING) {
      any_opening = true;
    } else if (member->current_operation == cover::COVER_OPERATION_CLOSING) {
      any_closing = true;
    }

    if (!member->has_state() || std::isnan(member->position)) {
      continue;
    }

    position_total += member->position;
    positioned_members++;
  }

  if (positioned_members == 0) {
    this->status_set_warning();
    this->current_operation = cover::COVER_OPERATION_IDLE;
    this->position = NAN;
    this->set_has_state(false);
    this->publish_state();
    return;
  }

  this->status_clear_warning();
  this->set_has_state(true);
  this->position = position_total / static_cast<float>(positioned_members);

  if (any_opening && !any_closing) {
    this->current_operation = cover::COVER_OPERATION_OPENING;
  } else if (any_closing && !any_opening) {
    this->current_operation = cover::COVER_OPERATION_CLOSING;
  } else {
    this->current_operation = cover::COVER_OPERATION_IDLE;
  }

  this->publish_state();
}

}  // namespace arc_bridge_group
}  // namespace esphome

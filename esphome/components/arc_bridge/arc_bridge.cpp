#include "arc_bridge.h"

#include <cmath>
#include <cstdio>

#include "esphome/core/log.h"

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_bridge";

void ARCBridgeComponent::add_blind(ARCBlind *blind) {
  if (blind == nullptr)
    return;

  blind->set_parent(this);
  this->blinds_.push_back(blind);
}

void ARCBridgeComponent::send_move_command(const std::string &blind_id, uint8_t percent) {
  if (percent > 100)
    percent = 100;

  char payload[4] = {0};
  std::snprintf(payload, sizeof(payload), "%03u", static_cast<unsigned int>(percent));
  this->send_simple_command_(blind_id, 'm', payload);
}

void ARCBridgeComponent::send_open_command(const std::string &blind_id) {
  this->send_move_command(blind_id, 'o');
}

void ARCBridgeComponent::send_close_command(const std::string &blind_id) {
  this->send_move_command(blind_id, 'c');
}

void ARCBridgeComponent::send_stop_command(const std::string &blind_id) {
  this->send_simple_command_(blind_id, 's');
}

void ARCBridgeComponent::send_simple_command_(const std::string &blind_id, char command,
                                              const std::string &payload) {
  std::string frame;
  frame.reserve(1 + blind_id.size() + 1 + payload.size() + 1);
  frame.push_back('!');
  frame += blind_id;
  frame.push_back(command);
  frame += payload;
  frame.push_back(';');

  ESP_LOGD(TAG, "TX -> %s", frame.c_str());
  this->write_str(frame.c_str());
}

void ARCBlind::control(const cover::CoverCall &call) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Blind %s has no parent ARC bridge", this->blind_id_.c_str());
    return;
  }

  if (call.get_stop()) {
    this->parent_->send_stop_command(this->blind_id_);
    return;
  }

  if (call.get_position().has_value()) {
    float position = *call.get_position();
    if (position < 0.0f)
      position = 0.0f;
    if (position > 1.0f)
      position = 1.0f;

    if (position >= 0.999f) {
      this->parent_->send_open_command(this->blind_id_);
      return;
    }
    if (position <= 0.001f) {
      this->parent_->send_close_command(this->blind_id_);
      return;
    }

    // ARC protocol: 0 = open, 100 = closed. HA/ESPHome uses 1=open, 0=closed.
    uint8_t arc_percent = static_cast<uint8_t>(std::round((1.0f - position) * 100.0f));
    this->parent_->send_move_command(this->blind_id_, arc_percent);
    return;
  }
}

}  // namespace arc_bridge
}  // namespace esphome


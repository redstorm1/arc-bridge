#include "arc_bridge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_bridge";
static constexpr uint32_t QUERY_INTERVAL_MS = 5000;
static constexpr uint32_t QUERY_SPACING_MS = 150;
static constexpr uint32_t OFFLINE_TIMEOUT_MS = 60000;

void ARCBridgeComponent::add_blind(ARCBlind *blind) {
  if (blind == nullptr)
    return;

  blind->set_parent(this);
  this->blinds_.push_back(blind);

  const std::string &id = blind->get_blind_id();
  auto &state = this->blind_states_[id];
  state.blind = blind;
}

void ARCBridgeComponent::map_lq_sensor(const std::string &id, sensor::Sensor *s) {
  auto &state = this->blind_states_[id];
  state.link_quality_sensor = s;
}

void ARCBridgeComponent::map_status_sensor(const std::string &id, text_sensor::TextSensor *s) {
  auto &state = this->blind_states_[id];
  state.status_sensor = s;
}

ARCBridgeComponent::BlindState *ARCBridgeComponent::find_state_(const std::string &blind_id) {
  auto it = this->blind_states_.find(blind_id);
  if (it == this->blind_states_.end())
    return nullptr;
  return &it->second;
}

void ARCBridgeComponent::send_move_command(const std::string &blind_id, uint8_t percent) {
  if (percent > 100)
    percent = 100;

  char payload[4] = {0};
  std::snprintf(payload, sizeof(payload), "%03u", static_cast<unsigned int>(percent));
  this->send_simple_command_(blind_id, 'm', payload);
}

void ARCBridgeComponent::send_open_command(const std::string &blind_id) {
  this->send_simple_command_(blind_id, 'o');
}

void ARCBridgeComponent::send_close_command(const std::string &blind_id) {
  this->send_simple_command_(blind_id, 'c');
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

void ARCBridgeComponent::loop() {
  uint8_t byte;
  while (this->available()) {
    if (!this->read_byte(&byte))
      break;
    this->handle_incoming_byte_(static_cast<char>(byte));
  }

  this->schedule_queries_();
  this->check_timeouts_();
}

void ARCBridgeComponent::handle_incoming_byte_(char byte) {
  if (byte == '!') {
    this->rx_buffer_.clear();
    this->rx_buffer_.push_back(byte);
    return;
  }

  if (this->rx_buffer_.empty())
    return;

  this->rx_buffer_.push_back(byte);

  if (this->rx_buffer_.size() > 64) {
    ESP_LOGW(TAG, "RX frame too long, discarding");
    this->rx_buffer_.clear();
    return;
  }

  if (byte == ';') {
    ESP_LOGD(TAG, "RX <- %s", this->rx_buffer_.c_str());
    this->process_frame_(this->rx_buffer_);
    this->rx_buffer_.clear();
  }
}

void ARCBridgeComponent::process_frame_(const std::string &frame) {
  if (frame.size() < 5 || frame.front() != '!' || frame.back() != ';')
    return;

  std::string payload = frame.substr(1, frame.size() - 2);
  if (payload.size() < 4)
    return;

  std::string blind_id = payload.substr(0, 3);
  std::string message = payload.substr(3);

  auto *state = this->find_state_(blind_id);
  if (state == nullptr) {
    ESP_LOGW(TAG, "Unknown blind id in frame: %s", frame.c_str());
    return;
  }

  const uint32_t now = millis();
  state->last_response_ms = now;
  state->pending_query = false;

  if (message.empty())
    return;

  if (message[0] == 'r') {
    if (message.size() >= 2 && message[1] == '?') {
      // query echo, ignore
      return;
    }

    std::string value_str = message.substr(1);
    if (value_str.empty())
      return;

    char *endptr = nullptr;
    long value = std::strtol(value_str.c_str(), &endptr, 10);
    if (endptr == value_str.c_str() || value < 0 || value > 100) {
      ESP_LOGW(TAG, "Invalid position value '%s'", value_str.c_str());
      return;
    }

    if (state->blind != nullptr) {
      state->blind->handle_position_report(static_cast<uint8_t>(value));
    }

    this->handle_status_update_(*state, blind_id, "online");
    return;
  }

  if (message == "Enp") {
    this->handle_status_update_(*state, blind_id, "not paired");
    return;
  }

  if (message == "Enl") {
    this->handle_status_update_(*state, blind_id, "not listening");
    return;
  }

  if (message[0] == 'R') {
    size_t offset = 1;
    if (message.size() >= 2 && message[1] == 'A')
      offset = 2;

    std::string value_str = message.substr(offset);
    if (!value_str.empty()) {
      char *endptr = nullptr;
      long value = std::strtol(value_str.c_str(), &endptr, 10);
      if (endptr != value_str.c_str() && value >= 0) {
        this->handle_link_quality_(*state, static_cast<float>(value));
        this->handle_status_update_(*state, blind_id, "online");
      }
    }
    return;
  }

  ESP_LOGD(TAG, "Unhandled frame payload '%s'", message.c_str());
}

void ARCBridgeComponent::handle_status_update_(BlindState &state, const std::string &blind_id,
                                               const std::string &status) {
  if (state.status == status)
    return;

  state.status = status;
  if (state.status_sensor != nullptr) {
    state.status_sensor->publish_state(status);
  }

  ESP_LOGD(TAG, "%s status -> %s", blind_id.c_str(), status.c_str());
}

void ARCBridgeComponent::handle_link_quality_(BlindState &state, float value) {
  if (state.link_quality_sensor == nullptr)
    return;

  state.link_quality_sensor->publish_state(value);
}

void ARCBridgeComponent::schedule_queries_() {
  const uint32_t now = millis();
  if (now - this->last_poll_ms_ < QUERY_SPACING_MS)
    return;

  this->last_poll_ms_ = now;

  for (auto &kv : this->blind_states_) {
    auto &state = kv.second;
    if (state.pending_query)
      continue;
    if (now - state.last_query_ms < QUERY_INTERVAL_MS)
      continue;

    this->send_simple_command_(kv.first, 'r', "?");
    state.last_query_ms = now;
    state.pending_query = true;
    break;
  }
}

void ARCBridgeComponent::check_timeouts_() {
  const uint32_t now = millis();
  for (auto &kv : this->blind_states_) {
    auto &state = kv.second;

    if (!state.status.empty() && state.status != "online")
      continue;

    if (state.last_response_ms == 0)
      continue;

    if (now - state.last_response_ms > OFFLINE_TIMEOUT_MS) {
      this->handle_status_update_(state, kv.first, "offline");
    }
  }
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

void ARCBlind::handle_position_report(uint8_t arc_percent) {
  if (arc_percent > 100)
    arc_percent = 100;

  float position = 1.0f - (static_cast<float>(arc_percent) / 100.0f);
  if (position < 0.0f)
    position = 0.0f;
  if (position > 1.0f)
    position = 1.0f;

  this->position = position;
  this->current_operation = cover::COVER_OPERATION_IDLE;
  this->publish_state();
}

}  // namespace arc_bridge
}  // namespace esphome


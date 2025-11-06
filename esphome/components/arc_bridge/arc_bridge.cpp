#include "arc_bridge.h"

#include <cmath>
#include <cstdio>
#include <regex>
#include <cstdlib>

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

void ARCBridgeComponent::send_position_query(const std::string &blind_id) {
  // send "!IDr?;"
  std::string frame;
  frame.reserve(1 + blind_id.size() + 2 + 1);
  frame.push_back('!');
  frame += blind_id;
  frame.push_back('r');
  frame.push_back('?');
  frame.push_back(';');

  ESP_LOGD(TAG, "TX (pos query) -> %s", frame.c_str());
  this->write_str(frame.c_str());
}

void ARCBridgeComponent::handle_incoming_frame(const std::string &frame) {
  // frame expected to start with '!' and end with ';'
  if (frame.empty() || frame.front() != '!') {
    ESP_LOGW(TAG, "Unexpected frame (no '!'): %s", frame.c_str());
    return;
  }

  // strip leading '!' and trailing ';' if present
  std::string body = frame.substr(1);
  if (!body.empty() && body.back() == ';')
    body.pop_back();

  // extract blind id: consecutive alnum chars from start
  size_t i = 0;
  while (i < body.size() && std::isalnum(static_cast<unsigned char>(body[i])))
    ++i;
  std::string blind_id = body.substr(0, i);
  std::string rest = (i < body.size()) ? body.substr(i) : "";

  // simple regex-based extraction for tokens like Enp=123, Enl=0, R=45, RA=67, r=010 etc.
  std::smatch m;
  std::regex re_enp(R"((?:Enp)\s*=\s*([0-9]+))", std::regex::icase);
  std::regex re_enl(R"((?:Enl)\s*=\s*([0-9]+))", std::regex::icase);
  std::regex re_r(R"((?:\bR)\s*=\s*([0-9]+))", std::regex::icase);
  std::regex re_ra(R"((?:RA)\s*=\s*([0-9]+))", std::regex::icase);
  std::regex re_pos(R"((?:\br)\s*[:=]\s*([0-9]+))", std::regex::icase);

  int enp = -1, enl = -1, r = -1, ra = -1, pos = -1;

  if (std::regex_search(rest, m, re_enp) && m.size() > 1) {
    const char *s = m[1].str().c_str();
    enp = static_cast<int>(std::strtol(s, nullptr, 10));
  }
  if (std::regex_search(rest, m, re_enl) && m.size() > 1) {
    const char *s = m[1].str().c_str();
    enl = static_cast<int>(std::strtol(s, nullptr, 10));
  }
  if (std::regex_search(rest, m, re_r) && m.size() > 1) {
    const char *s = m[1].str().c_str();
    r = static_cast<int>(std::strtol(s, nullptr, 10));
  }
  if (std::regex_search(rest, m, re_ra) && m.size() > 1) {
    const char *s = m[1].str().c_str();
    ra = static_cast<int>(std::strtol(s, nullptr, 10));
  }
  if (std::regex_search(rest, m, re_pos) && m.size() > 1) {
    const char *s = m[1].str().c_str();
    pos = static_cast<int>(std::strtol(s, nullptr, 10));
  }

  ESP_LOGD(TAG, "RX <- id=%s rest=\"%s\"", blind_id.c_str(), rest.c_str());
  if (enp >= 0) ESP_LOGD(TAG, "  Enp=%d", enp);
  if (enl >= 0) ESP_LOGD(TAG, "  Enl=%d", enl);
  if (r >= 0) ESP_LOGD(TAG, "  R=%d", r);
  if (ra >= 0) ESP_LOGD(TAG, "  RA=%d", ra);
  if (pos >= 0) ESP_LOGD(TAG, "  r(pos)=%d", pos);

  // Optional: locate the ARCBlind and update sensors/state.
  // ARCBlind *b = this->find_blind_by_id(blind_id);
  // if (b && pos >= 0) { ... convert and publish position ... }

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


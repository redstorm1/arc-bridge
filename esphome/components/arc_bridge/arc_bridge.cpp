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
  ESP_LOGD(TAG, "add_blind(): registering blind object (id='%s')",
           blind->get_blind_id().c_str());
  blind->set_parent(this);
  this->blinds_.push_back(blind);
}

void ARCBridgeComponent::map_lq_sensor(const std::string &id, sensor::Sensor *s) {
  ESP_LOGD(TAG, "map_lq_sensor(): mapping lq sensor for id='%s'", id.c_str());
  lq_map_[id] = s;
}

void ARCBridgeComponent::map_status_sensor(const std::string &id, text_sensor::TextSensor *s) {
  ESP_LOGD(TAG, "map_status_sensor(): mapping status sensor for id='%s'", id.c_str());
  status_map_[id] = s;
}

ARCBlind *ARCBridgeComponent::find_blind_by_id(const std::string &id) {
  for (auto *b : this->blinds_) {
    if (b == nullptr) continue;
    ESP_LOGD(TAG, "find_blind_by_id(): checking blind id='%s' against '%s'",
             b->get_blind_id().c_str(), id.c_str());
    if (b->get_blind_id() == id)
      return b;
  }
  return nullptr;
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

  // regex-based extraction for tokens like Enp=123, Enl=0, R=45, RA=67, r=010 etc.
  std::smatch m;
  std::regex re_enp(R"((?:Enp)\s*[:=]?\s*([0-9]+))", std::regex::icase);
  std::regex re_enl(R"((?:Enl)\s*[:=]?\s*([0-9]+))", std::regex::icase);
  std::regex re_r(R"((?:\bR)\s*[:=]?\s*([0-9]+))", std::regex::icase);
  std::regex re_ra(R"((?:RA)\s*[:=]?\s*([0-9]+))", std::regex::icase);
  std::regex re_pos(R"((?:\br)\s*[:=]?\s*([0-9]+))", std::regex::icase);

  int enp = -1, enl = -1, r = -1, ra = -1, pos = -1;

  if (std::regex_search(rest, m, re_enp) && m.size() > 1) {
    enp = static_cast<int>(std::strtol(m[1].str().c_str(), nullptr, 10));
  }
  if (std::regex_search(rest, m, re_enl) && m.size() > 1) {
    enl = static_cast<int>(std::strtol(m[1].str().c_str(), nullptr, 10));
  }
  if (std::regex_search(rest, m, re_r) && m.size() > 1) {
    r = static_cast<int>(std::strtol(m[1].str().c_str(), nullptr, 10));
  }
  if (std::regex_search(rest, m, re_ra) && m.size() > 1) {
    ra = static_cast<int>(std::strtol(m[1].str().c_str(), nullptr, 10));
  }
  if (std::regex_search(rest, m, re_pos) && m.size() > 1) {
    pos = static_cast<int>(std::strtol(m[1].str().c_str(), nullptr, 10));
  }

  ESP_LOGD(TAG, "RX <- id=%s rest=\"%s\"", blind_id.c_str(), rest.c_str());
  if (enp >= 0) ESP_LOGD(TAG, "  Enp=%d", enp);
  if (enl >= 0) ESP_LOGD(TAG, "  Enl=%d", enl);
  if (r >= 0) ESP_LOGD(TAG, "  R=%d", r);
  if (ra >= 0) ESP_LOGD(TAG, "  RA=%d", ra);
  if (pos >= 0) ESP_LOGD(TAG, "  r(pos)=%d", pos);

  // publish/update link-quality sensor (prefer RA then R)
  auto it_lq = this->lq_map_.find(blind_id);
  if (it_lq != this->lq_map_.end() && it_lq->second != nullptr) {
    float lq_val = -1.0f;
    if (ra >= 0)
      lq_val = static_cast<float>(ra);
    else if (r >= 0)
      lq_val = static_cast<float>(r);
    if (lq_val >= 0.0f)
      it_lq->second->publish_state(lq_val);
  }

  // publish/update status text (Enp/Enl)
  auto it_status = this->status_map_.find(blind_id);
  if (it_status != this->status_map_.end() && it_status->second != nullptr) {
    std::string status;
    if (enp >= 0) {
      if (!status.empty()) status += " ";
      status += "Enp=" + std::to_string(enp);
    }
    if (enl >= 0) {
      if (!status.empty()) status += " ";
      status += "Enl=" + std::to_string(enl);
    }
    if (!status.empty())
      it_status->second->publish_state(status);
  }

  // update cover position if we can find a matching blind and pos is present
  if (pos >= 0) {
    // pos from device: 0 = open, 100 = closed -> HA/ESPhome uses 1.0 = open, 0.0 = closed
    float ha_pos = 1.0f - (static_cast<float>(pos) / 100.0f);
    ARCBlind *b = this->find_blind_by_id(blind_id);
    if (b != nullptr) {
      ESP_LOGD(TAG, "Publishing position %.3f to blind '%s'", ha_pos, blind_id.c_str());
      b->publish_position(ha_pos);
    } else {
      ESP_LOGW(TAG, "No ARCBlind registered for id='%s' - position ignored", blind_id.c_str());
    }
  }
}

// ARCBlind methods
void ARCBlind::setup() {
  // keep ignore_control_ true until we receive a real position from the bridge
  // to avoid acting on HA restore/optimistic commands on startup.
}

void ARCBlind::set_name(const std::string &name) {
  // store the name in the instance so the c_str() pointer remains valid
  this->name_ = name;
  cover::Cover::set_name(this->name_.c_str());
}

void ARCBlind::publish_position(float position) {
  // store last and publish state to the cover (position 0..1)
  this->last_published_position_ = position;
  this->publish_state(position);

  // clear the startup guard when we receive the first real position
  if (this->ignore_control_) {
    this->ignore_control_ = false;
    ESP_LOGD(TAG, "Cleared ignore_control_ for blind '%s' after receiving position", this->blind_id_.c_str());
  }
}

void ARCBlind::control(const cover::CoverCall &call) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Blind has no parent ARC bridge");
    return;
  }

  // ignore early control calls during init to avoid unwanted startup moves
  if (this->ignore_control_) {
    ESP_LOGD(TAG, "Ignoring control for %s during init", this->blind_id_.c_str());
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


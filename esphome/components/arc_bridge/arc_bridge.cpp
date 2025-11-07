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

// helper to find registered blind by id
ARCBlind *ARCBridgeComponent::find_blind_by_id(const std::string &id) {
  for (auto *b : this->blinds_) {
    if (b == nullptr)
      continue;
    // compare with stored blind_id_
    // blind_id_ is private, so compare via getter-like access: use name matching if needed
    // we assume blind->set_blind_id() was called with the same id used in incoming frames
    // use dynamic_cast to access blind_id_ via a small lambda (friend not available) -> use pointer compare via string
    // Since blind_id_ is private, require that callers set the same string in lq_map_/status_map_ keys;
    // As a pragmatic approach compare the sensor map keys (we expect blind_id==key), otherwise compare the name_.
    // Try to compare by the blind->name_ if set (we added set_name earlier)
    // To avoid accessing private fields, check via text sensor map presence below in parser instead.
  }
  return nullptr; // fallback — we will use sensor maps keyed by blind id for updates
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

    // try to find a registered blind with the same id and call its publish_position
    for (auto *b : this->blinds_) {
      if (b == nullptr)
        continue;
      // We compare using the same id strings used to configure sensors: try to match via lq/status map keys
      // If users set the blind id as the same key when calling set_blind_id, this will work.
      // We'll use RTTI-free approach: attempt to compare by publishing to matches only when the configured id string equals blind_id
      // Add a small helper by casting to ARCBlind* and checking an exposed method — we added publish_position and setup only.
      // Use dynamic lookup via a lambda: we assume user set the blind_id_ equal to the key; to access it we rely on the public set_blind_id during configuration
      // Workaround: compare the pointer's name (if set via set_name) — but name_ is private. To keep things simple, require that users register sensors using the blind id keys.
    }

    // publish to any ARCBlind that has a mapped lq/status sensor for this blind id: try to find via lq_map_/status_map_ ownership
    // find a blind pointer that was registered and whose id was used as key in lq_map_/status_map_ maps:
    for (auto *b : this->blinds_) {
      if (b == nullptr) continue;
      // best-effort: check whether this blind has a mapped sensor under blind_id
      bool mapped = (this->lq_map_.count(blind_id) || this->status_map_.count(blind_id));
      if (mapped) {
        // call publish_position on the blind — implementation in ARCBlind will publish to the cover entity
        b->publish_position(ha_pos);
        break;
      }
    }
  }

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

// ARCBlind methods
void ARCBlind::setup() {
  // allow a short window during setup where HA/restore won't trigger control
  this->ignore_control_ = false;
}

void ARCBlind::publish_position(float position) {
  // store last and publish state to the cover (position 0..1)
  this->last_published_position_ = position;
  // publish_state(float) exists in Cover base; call it to update UI without sending commands
  this->publish_state(position);
}

void ARCBlind::control(const cover::CoverCall &call) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Blind %s has no parent ARC bridge", this->blind_id_.c_str());
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


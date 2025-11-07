#include "arc_bridge.h"

#include <Arduino.h>   // millis()
#include <cctype>      // std::isalnum
#include <cstdio>      // snprintf
#include <cstdlib>     // strtol
#include <regex>
#include <string>

#include "esphome/core/log.h"

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_bridge";

// move to file scope so they're constructed once
static const std::regex RE_ENP(R"((?:Enp)\s*[:=]?\s*([0-9]+))", std::regex::icase);
static const std::regex RE_ENL(R"((?:Enl)\s*[:=]?\s*([0-9]+))", std::regex::icase);
static const std::regex RE_RHEX(R"((?:\bR)\s*[:=]?\s*([0-9A-Fa-f]{2}))");
static const std::regex RE_RPOS(R"((?:\br)\s*[:=]?\s*([0-9]+))");
static const std::regex RE_ID(R"(^([A-Z]{2,4})(.*))");

// ───────────────────────────────────────────────────────────────────────────────
// ARCBridgeComponent
// ───────────────────────────────────────────────────────────────────────────────

void ARCBridgeComponent::setup() {
  this->boot_millis_ = millis();
  this->startup_guard_cleared_ = false;
  ESP_LOGD(TAG, "ARCBridgeComponent setup; startup guard %u ms", STARTUP_GUARD_MS);
}

void ARCBridgeComponent::loop() {
  // 1) startup guard: clear ignore_control_ on blinds after grace period
  if (!this->startup_guard_cleared_) {
    const uint32_t now = millis();
    if (now - this->boot_millis_ >= STARTUP_GUARD_MS) {
      for (auto *b : this->blinds_) {
        if (b) b->clear_startup_guard();
      }
      this->startup_guard_cleared_ = true;
      ESP_LOGD(TAG, "Startup guard cleared for all blinds");
    }
  }

  // 2) UART: accumulate bytes; extract '!'...' ;' framed messages
  while (this->available()) {
    const int c = this->read();
    if (c < 0) break;
    this->rx_buffer_.push_back(static_cast<char>(c));

    // find start '!' and terminator ';'
    size_t start = this->rx_buffer_.find('!');
    if (start == std::string::npos) {
      // avoid runaway buffer pre-start
      if (this->rx_buffer_.size() > 256) this->rx_buffer_.clear();
      break;
    }

    size_t term = this->rx_buffer_.find(';', start);
    if (term == std::string::npos) {
      // trim garbage before '!' and wait for more
      if (start > 0) this->rx_buffer_.erase(0, start);
      break;
    }

    // full frame ready
    std::string frame = this->rx_buffer_.substr(start, term - start + 1);
    this->rx_buffer_.erase(0, term + 1);
    this->handle_incoming_frame(frame);
  }

  // 3) periodic round-robin position queries
  const uint32_t now_q = millis();
  if (now_q - this->last_query_millis_ >= QUERY_INTERVAL_MS && !this->blinds_.empty()) {
    this->last_query_millis_ = now_q;
    if (this->query_index_ >= this->blinds_.size()) this->query_index_ = 0;
    ARCBlind *b = this->blinds_[this->query_index_];
    if (b != nullptr) {
      ESP_LOGD(TAG, "Periodic position query -> id='%s'", b->get_blind_id().c_str());
      this->send_position_query(b->get_blind_id());
    }
    this->query_index_ = (this->query_index_ + 1) % this->blinds_.size();
  }
}

void ARCBridgeComponent::add_blind(ARCBlind *blind) {
  if (!blind) return;
  blind->set_parent(this);
  this->blinds_.push_back(blind);
  ESP_LOGD(TAG, "Registered blind id='%s'", blind->get_blind_id().c_str());
}

ARCBlind *ARCBridgeComponent::find_blind_by_id(const std::string &id) {
  for (auto *b : this->blinds_) {
    if (b && b->get_blind_id() == id) return b;
  }
  return nullptr;
}

void ARCBridgeComponent::map_lq_sensor(const std::string &id, sensor::Sensor *s) {
  lq_map_[id] = s;
  ESP_LOGD(TAG, "Mapped link quality sensor for id='%s'", id.c_str());
}

void ARCBridgeComponent::map_status_sensor(const std::string &id, text_sensor::TextSensor *s) {
  status_map_[id] = s;
  ESP_LOGD(TAG, "Mapped status text sensor for id='%s'", id.c_str());
}

// ── Commands ───────────────────────────────────────────────────────────────────

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
  if (percent > 100) percent = 100;
  char payload[4] = {0};
  std::snprintf(payload, sizeof(payload), "%03u", static_cast<unsigned>(percent));
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
  // !IDr?;
  std::string frame;
  frame.reserve(1 + blind_id.size() + 3);
  frame.push_back('!');
  frame += blind_id;
  frame.push_back('r');
  frame.push_back('?');
  frame.push_back(';');
  ESP_LOGD(TAG, "TX (pos query) -> %s", frame.c_str());
  this->write_str(frame.c_str());
}

void ARCBridgeComponent::request_position_now(const std::string &blind_id) {
  this->send_position_query(blind_id);
}

// ── Parser ─────────────────────────────────────────────────────────────────────

void ARCBridgeComponent::handle_incoming_frame(const std::string &frame) {
  // log raw frame
  ESP_LOGD(TAG, "RX raw -> %s", frame.c_str());

  if (frame.empty() || frame.front() != '!') {
    ESP_LOGW(TAG, "Unexpected frame (no '!'): %s", frame.c_str());
    return;
  }

  std::string body = frame.substr(1);
  if (!body.empty() && body.back() == ';')
    body.pop_back();

  // --- extract blind id (leading 2..4 uppercase letters) ---
  size_t idx = 0;
  while (idx < body.size() && std::isupper(static_cast<unsigned char>(body[idx])) && idx < 4)
    ++idx;
  std::string blind_id;
  if (idx >= 1)
    blind_id = body.substr(0, idx);
  else {
    // fallback: take initial alnum run
    idx = 0;
    while (idx < body.size() && std::isalnum(static_cast<unsigned char>(body[idx])))
      ++idx;
    blind_id = body.substr(0, idx);
  }
  std::string rest = (idx < body.size()) ? body.substr(idx) : "";

  // small helpers
  auto skip_delims = [](const std::string &s, size_t &i) {
    while (i < s.size()) {
      unsigned char c = static_cast<unsigned char>(s[i]);
      if (c == ' ' || c == ':' || c == '=' || c == ',' || c == ';')
        ++i;
      else
        break;
    }
  };
  auto match_ci = [](const std::string &s, size_t pos, const char *tok) -> bool {
    size_t n = std::strlen(tok);
    if (pos + n > s.size()) return false;
    for (size_t k = 0; k < n; ++k) {
      if (std::toupper(static_cast<unsigned char>(s[pos + k])) != std::toupper(static_cast<unsigned char>(tok[k])))
        return false;
    }
    return true;
  };

  // parsed values
  int enp = -1;
  int enl = -1;
  bool enp_key = false;
  bool enl_key = false;
  int r_raw = -1;   // raw hex 0x00..0xFF
  int pos_val = -1; // numerical position (device 0..100)

  // scan rest for tokens (fast manual parse)
  size_t i = 0;
  while (i < rest.size()) {
    // skip separators
    if (rest[i] == ' ' || rest[i] == ',' || rest[i] == ';') { ++i; continue; }

    // Enp (case-insensitive) maybe followed by =number or standalone
    if (match_ci(rest, i, "Enp")) {
      enp_key = true;
      i += 3;
      size_t j = i;
      skip_delims(rest, j);
      // parse digits if present
      if (j < rest.size() && std::isdigit(static_cast<unsigned char>(rest[j]))) {
        int val = 0;
        while (j < rest.size() && std::isdigit(static_cast<unsigned char>(rest[j]))) {
          val = val * 10 + (rest[j] - '0');
          ++j;
        }
        enp = val;
        i = j;
        continue;
      } else {
        i = j;
        continue;
      }
    }

    // Enl (case-insensitive)
    if (match_ci(rest, i, "Enl")) {
      enl_key = true;
      i += 3;
      size_t j = i;
      skip_delims(rest, j);
      if (j < rest.size() && std::isdigit(static_cast<unsigned char>(rest[j]))) {
        int val = 0;
        while (j < rest.size() && std::isdigit(static_cast<unsigned char>(rest[j]))) {
          val = val * 10 + (rest[j] - '0');
          ++j;
        }
        enl = val;
        i = j;
        continue;
      } else {
        i = j;
        continue;
      }
    }

    // lowercase r = position (digits)
    if (rest[i] == 'r') {
      size_t j = i + 1;
      skip_delims(rest, j);
      if (j < rest.size() && std::isdigit(static_cast<unsigned char>(rest[j]))) {
        int val = 0;
        while (j < rest.size() && std::isdigit(static_cast<unsigned char>(rest[j]))) {
          val = val * 10 + (rest[j] - '0');
          ++j;
        }
        pos_val = val;
        i = j;
        continue;
      } else {
        ++i;
        continue;
      }
    }

    // uppercase R = radio raw value (hex 1-2 digits, prefer 2)
    if (rest[i] == 'R') {
      size_t j = i + 1;
      skip_delims(rest, j);
      // collect up to 2 hex digits
      int val = 0;
      size_t digits = 0;
      while (j < rest.size() && digits < 2) {
        unsigned char c = static_cast<unsigned char>(rest[j]);
        int nib = -1;
        if (c >= '0' && c <= '9') nib = c - '0';
        else if (c >= 'A' && c <= 'F') nib = 10 + (c - 'A');
        else if (c >= 'a' && c <= 'f') nib = 10 + (c - 'a');
        else break;
        val = val * 16 + nib;
        ++digits;
        ++j;
      }
      if (digits > 0) {
        r_raw = val;
        i = j;
        continue;
      } else {
        ++i;
        continue;
      }
    }

    // unknown token: advance
    ++i;
  }

  // summarise for log
  std::string summary = "id=" + blind_id;
  if (enp >= 0) summary += " Enp=" + std::to_string(enp);
  else if (enp_key) summary += " Enp";
  if (enl >= 0) summary += " Enl=" + std::to_string(enl);
  else if (enl_key) summary += " Enl";
  if (r_raw >= 0) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", r_raw);
    summary += " R=" + std::string(buf);
  }
  if (pos_val >= 0) summary += " r=" + std::to_string(pos_val);

  ESP_LOGD(TAG, "RX parsed -> %s ; rest=\"%s\"", summary.c_str(), rest.c_str());

  // publish link quality (from R hex only)
  auto it_lq = this->lq_map_.find(blind_id);
  if (it_lq != this->lq_map_.end() && it_lq->second != nullptr) {
    if (r_raw >= 0) {
      float lq_val = (255.0f - static_cast<float>(r_raw)) * 100.0f / 255.0f;
      if (lq_val < 0.0f) lq_val = 0.0f;
      if (lq_val > 100.0f) lq_val = 100.0f;
      it_lq->second->publish_state(lq_val);
    }
  }

  // publish status text (Enp/Enl)
  auto it_status = this->status_map_.find(blind_id);
  if (it_status != this->status_map_.end() && it_status->second != nullptr) {
    std::string status;
    if (enp >= 0) status += "Enp=" + std::to_string(enp);
    else if (enp_key) status += "Enp";
    if (enl >= 0) {
      if (!status.empty()) status += " ";
      status += "Enl=" + std::to_string(enl);
    } else if (enl_key) {
      if (!status.empty()) status += " ";
      status += "Enl";
    }
    if (!status.empty()) {
      ESP_LOGD(TAG, "Publishing status for id='%s' -> %s", blind_id.c_str(), status.c_str());
      it_status->second->publish_state(status);
    }
  }

  // update cover position if present
  if (pos_val >= 0) {
    ARCBlind *b = this->find_blind_by_id(blind_id);
    if (b != nullptr) {
      ESP_LOGD(TAG, "Publishing raw position %d to blind '%s'", pos_val, blind_id.c_str());
      b->publish_raw_position(pos_val);
    } else {
      ESP_LOGW(TAG, "No ARCBlind registered for id='%s' - position ignored", blind_id.c_str());
    }
  }
}

// ───────────────────────────────────────────────────────────────────────────────
// ARCBlind (cover)
// ───────────────────────────────────────────────────────────────────────────────

void ARCBlind::setup() {
  // nothing special required; traits declare position support
}

void ARCBlind::clear_startup_guard() {
  if (!this->ignore_control_) return;
  this->ignore_control_ = false;
  ESP_LOGD(TAG, "Cleared ignore_control_ for blind '%s' after receiving position", this->blind_id_.c_str());
}

void ARCBlind::publish_raw_position(int device_pos) {
  if (device_pos < 0) device_pos = 0;
  if (device_pos > 100) device_pos = 100;

  float ha_pos = 0.0f;
  // device: 0=open, 100=closed
  if (this->invert_position_) {
    // invert mapping if user prefers device semantics
    ha_pos = static_cast<float>(device_pos) / 100.0f;         // 0..1 open
  } else {
    // default HA semantics: 1=open, 0=closed
    ha_pos = 1.0f - (static_cast<float>(device_pos) / 100.0f);
  }
  this->publish_position(ha_pos);
}

void ARCBlind::set_name(const std::string &name) {
  this->name_ = name;
  cover::Cover::set_name(this->name_.c_str());
}

void ARCBlind::publish_position(float position) {
  if (position < 0.0f) position = 0.0f;
  if (position > 1.0f) position = 1.0f;

  this->last_published_position_ = position;
  this->publish_state(position);

  // first valid position received → controls safe to accept
  if (this->ignore_control_) {
    this->ignore_control_ = false;
    ESP_LOGD(TAG, "Control enabled for '%s' after first position", this->blind_id_.c_str());
  }
}

void ARCBlind::control(const cover::CoverCall &call) {
  if (!this->parent_) {
    ESP_LOGW(TAG, "Blind '%s' has no parent ARC bridge", this->blind_id_.c_str());
    return;
  }

  if (this->ignore_control_) {
    ESP_LOGD(TAG, "Ignoring control for '%s' during startup guard", this->blind_id_.c_str());
    return;
  }

  if (call.get_stop()) {
    this->parent_->send_stop_command(this->blind_id_);
    return;
  }

  if (call.get_position().has_value()) {
    float p = *call.get_position();
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;

    // HA 1=open, 0=closed  ↔  device 0=open, 100=closed
    // we keep default (invert_position_=false) mapping that users expect in HA UI
    uint8_t arc_percent = static_cast<uint8_t>(std::round((1.0f - p) * 100.0f));
    if (arc_percent >= 100) { this->parent_->send_close_command(this->blind_id_); return; }
    if (arc_percent <= 0)   { this->parent_->send_open_command(this->blind_id_);  return; }
    this->parent_->send_move_command(this->blind_id_, arc_percent);
    return;
  }

  // (tilt support can be added later)
}

void ARCBlind::set_invert_position(bool invert) { this->invert_position_ = invert; }

cover::CoverTraits ARCBlind::get_traits() {
  cover::CoverTraits t;
  t.set_supports_position(true);
  t.set_supports_tilt(false);
  return t;
}


}  // namespace arc_bridge
}  // namespace esphome

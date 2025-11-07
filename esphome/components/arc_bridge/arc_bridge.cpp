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
  ESP_LOGD(TAG, "RX raw -> %s", frame.c_str());
  if (frame.empty() || frame.front() != '!') return;

  // strip '!' and trailing ';'
  std::string body = frame.substr(1);
  if (!body.empty() && body.back() == ';') body.pop_back();

  // ID = 2–4 uppercase letters at start
  std::string blind_id, rest;
  {
    std::smatch idm;
    std::regex re_id(R"(^([A-Z]{2,4})(.*))");
    if (std::regex_match(body, idm, re_id)) {
      blind_id = idm[1].str();
      rest = idm[2].str();
    } else {
      // fallback: alnum prefix as id
      size_t i = 0;
      while (i < body.size() && std::isalnum(static_cast<unsigned char>(body[i]))) ++i;
      blind_id = body.substr(0, i);
      rest = (i < body.size()) ? body.substr(i) : "";
    }
  }

  // parse tokens
  std::smatch m;
  // r### b### ,Rxx    (we accept any order after ID)
  std::regex re_status(R"(r(\d{3})b(\d{3}),R([0-9A-Fa-f]{2}))");
  std::regex re_pos_only(R"(r(\d{3}))");
  std::regex re_rssi(R"(,R([0-9A-Fa-f]{2}))");

  // errors like !IDEnl;  or !IDEnp;
  std::regex re_en(R"(^E(n[pl])$)", std::regex::icase);     // whole rest is Enl/Enp
  std::regex re_en_any(R"(E(n[pl]))", std::regex::icase);   // Enl/Enp anywhere

  int pos = -1;
  int tilt = -1;
  int r_raw = -1;
  bool have_enl = false, have_enp = false;

  // full status first
  if (std::regex_search(rest, m, re_status)) {
    pos = std::stoi(m[1]);
    tilt = std::stoi(m[2]);
    r_raw = static_cast<int>(std::strtol(m[3].str().c_str(), nullptr, 16));
  } else {
    // partials
    if (std::regex_search(rest, m, re_pos_only)) pos = std::stoi(m[1]);
    if (std::regex_search(rest, m, re_rssi)) r_raw = static_cast<int>(std::strtol(m[1].str().c_str(), nullptr, 16));

    // error detection
    if (std::regex_match(rest, m, re_en)) {
      std::string code = m[1];
      have_enl = (code == "nl" || code == "NL");
      have_enp = (code == "np" || code == "NP");
    } else if (std::regex_search(rest, m, re_en_any)) {
      std::string code = m[1];
      have_enl = have_enl || (code == "nl" || code == "NL");
      have_enp = have_enp || (code == "np" || code == "NP");
    }
  }

  // log summary
  {
    char buf[16] = {0};
    std::string summary = "id=" + blind_id;
    if (pos >= 0) summary += " r=" + std::to_string(pos);
    if (tilt >= 0) summary += " b=" + std::to_string(tilt);
    if (r_raw >= 0) { std::snprintf(buf, sizeof(buf), "0x%02X", r_raw); summary += " R=" + std::string(buf); }
    if (have_enl) summary += " Enl";
    if (have_enp) summary += " Enp";
    ESP_LOGD(TAG, "RX parsed -> %s ; rest=\"%s\"", summary.c_str(), rest.c_str());
  }

  // publish outputs
  if (r_raw >= 0) this->publish_link_quality_(blind_id, r_raw);
  if (have_enl || have_enp) this->publish_status_(blind_id, have_enp, have_enl, -1, -1);
  this->publish_position_if_known_(blind_id, pos);
}

void ARCBridgeComponent::publish_link_quality_(const std::string &id, int r_raw_hex) {
  auto it = this->lq_map_.find(id);
  if (it == this->lq_map_.end() || it->second == nullptr) return;
  // LinkQuality% = 100 * (255 - raw) / 255  (inverse of RSSI)
  float pct = (255.0f - static_cast<float>(r_raw_hex)) * 100.0f / 255.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  it->second->publish_state(pct);
}

void ARCBridgeComponent::publish_status_(const std::string &id, bool have_enp, bool have_enl,
                                         int /*enp_val*/, int /*enl_val*/) {
  auto it = this->status_map_.find(id);
  if (it == this->status_map_.end() || it->second == nullptr) return;

  std::string status = "OK";
  if (have_enp) status = "Not Paired";
  else if (have_enl) status = "Not Listening";

  it->second->publish_state(status);
}

void ARCBridgeComponent::publish_position_if_known_(const std::string &id, int pos) {
  if (pos < 0) return;
  ARCBlind *b = this->find_blind_by_id(id);
  if (!b) {
    ESP_LOGW(TAG, "No blind registered for id='%s' (position %d ignored)", id.c_str(), pos);
    return;
  }
  b->publish_raw_position(pos);
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
  ESP_LOGD(TAG, "Startup guard cleared for '%s'", this->blind_id_.c_str());
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

}  // namespace arc_bridge
}  // namespace esphome

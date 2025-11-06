#include "arc_bridge.h"
#include <cctype>

namespace esphome {
namespace sensor { class Sensor; }
namespace text_sensor { class TextSensor; }
namespace cover { class Cover; }

namespace arc_bridge {

// ================= ARCBlind ==================
void ARCBlind::control(const cover::CoverCall &call) {
  if (!bridge_) return;

  if (call.get_stop()) bridge_->send_cmd("!" + blind_id_ + "s;");
  if (call.get_open()) bridge_->send_cmd("!" + blind_id_ + "o;");
  if (call.get_close()) bridge_->send_cmd("!" + blind_id_ + "c;");

  if (call.get_position().has_value()) {
    float p01 = *call.get_position();
    int rf_pct = (int) roundf((1.0f - p01) * 100.0f);
    rf_pct = std::max(0, std::min(100, rf_pct));
    bridge_->send_move_to_percent(blind_id_, rf_pct);
  }
}

void ARCBlind::publish_pos(float p01) {
  this->position = p01;
  this->publish_state();
  this->mark_seen();
}

void ARCBlind::update_status(bool paired, bool online) {
  paired_ = paired;
  online_ = online;
  std::string s;
  if (!paired) s = "not paired";
  else if (!online) s = "offline";
  else s = "online";
  if (status_sensor_) status_sensor_->publish_state(s);
  ESP_LOGI("ARC", "[%s] status: %s", blind_id_.c_str(), s.c_str());
}

// ================= ARCBridge ==================
void ARCBridgeComponent::setup() {
  ESP_LOGI("ARC", "ARCBridge setup");
}

void ARCBridgeComponent::loop() {
  bool got = false;
  uint8_t c;
  while (available() && read_byte(&c)) {
    got = true;
    last_rx_ = millis();
    if (c != '\r' && c != '\n') {
      char shown = (c >= 32 && c <= 126) ? (char)c : '.';
      buf_.push_back(shown);
    }
    if (c == ';' || c == '\n') {
      if (!buf_.empty()) {
        parse_frame_(buf_);
        buf_.clear();
      }
    }
  }

  if (!got && !buf_.empty() && (millis() - last_rx_ > 40)) {
    parse_frame_(buf_);
    buf_.clear();
  }

  // check timeouts → mark offline
  for (auto &pair : blinds_) {
    ARCBlind *b = pair.second;
    if (b && b->paired() && b->online() && b->timeout(millis(), 60000)) {
      b->update_status(true, false);
    }
  }
}

// --- Bridge control helpers ---
void ARCBridgeComponent::send_cmd(const std::string &ascii) {
  std::string msg = ascii;
  if (msg.empty() || msg[0] != '!') msg.insert(0, "!");
  if (msg.back() != ';') msg.push_back(';');
  write_array((const uint8_t*)msg.data(), msg.size());
  ESP_LOGI("ARC", "TX: %s", msg.c_str());
}

void ARCBridgeComponent::send_move_to_percent(const std::string &blind, int pct) {
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "!%s m%03d;", blind.c_str(), pct);
  send_cmd(cmd);
}

void ARCBridgeComponent::add_blind(ARCBlind *b) {
  b->set_bridge(this);
  blinds_[b->blind_id()] = b;
}

void ARCBridgeComponent::map_lq_sensor(const std::string &blind, sensor::Sensor *s) {
  lq_sensors_[blind] = s;
}
void ARCBridgeComponent::map_status_sensor(const std::string &blind, text_sensor::TextSensor *s) {
  status_sensors_[blind] = s;
}

// --- Parser ---
void ARCBridgeComponent::parse_frame_(const std::string &f) {
  if (f.size() < 5 || f[0] != '!') return;
  std::string blind = f.substr(1, 3);

  // error states
  if (f.find("Enp") != std::string::npos) {
    on_status(blind, "not paired");
    on_lq(blind, 0);
    return;
  }
  if (f.find("Enl") != std::string::npos) {
    on_status(blind, "offline");
    on_lq(blind, 0);
    return;
  }

  // normal r### reply
  if (f.find('r') != std::string::npos || f.find('R') != std::string::npos) {
    int pct = -1;
    size_t rpos = f.find('r');
    if (rpos == std::string::npos) rpos = f.find('R');
    if (rpos != std::string::npos) {
      size_t i = rpos + 1;
      std::string num;
      while (i < f.size() && isdigit((unsigned char)f[i]) && num.size() < 3) num.push_back(f[i++]);
      if (!num.empty()) pct = atoi(num.c_str());
    }
    if (pct >= 0 && pct <= 100) {
      float p01 = 1.0f - (pct / 100.0f);
      on_pos(blind, p01);
      parse_rf_quality_(blind, f);
      on_status(blind, "online");
      ESP_LOGI("ARC", "[%s] %d%% closed → %.2f", blind.c_str(), pct, p01);
    }
  }
}

void ARCBridgeComponent::parse_rf_quality_(const std::string &blind, const std::string &frame) {
  int ra = -1;
  size_t rapos = frame.find(",RA");
  if (rapos == std::string::npos) rapos = frame.find(",R");
  if (rapos != std::string::npos) {
    size_t i = rapos + ((frame.size() > rapos+2 && frame[rapos + 2] == 'A') ? 3 : 2);
    std::string ran;
    while (i < frame.size() && isdigit((unsigned char)frame[i]) && ran.size() < 3) ran.push_back(frame[i++]);
    if (!ran.empty()) ra = atoi(ran.c_str());
  }
  if (ra >= 0) {
    int q = (ra > 10 && ra <= 99) ? ra : std::min(ra, 10) * 10;
    on_lq(blind, q);
  }
}

// --- Callbacks ---
void ARCBridgeComponent::on_pos(const std::string &blind, float p01) {
  auto it = blinds_.find(blind);
  if (it != blinds_.end() && it->second) it->second->publish_pos(p01);
}

void ARCBridgeComponent::on_lq(const std::string &blind, int qpct) {
  auto it = lq_sensors_.find(blind);
  if (it != lq_sensors_.end() && it->second) it->second->publish_state(qpct);
}

void ARCBridgeComponent::on_status(const std::string &blind, const std::string &state) {
  auto it = blinds_.find(blind);
  if (it != blinds_.end() && it->second) {
    if (state == "not paired") it->second->update_status(false, false);
    else if (state == "offline") it->second->update_status(true, false);
    else it->second->update_status(true, true);
  }
  auto st = status_sensors_.find(blind);
  if (st != status_sensors_.end() && st->second)
    st->second->publish_state(state);
}

}  // namespace arc_bridge
}  // namespace esphome

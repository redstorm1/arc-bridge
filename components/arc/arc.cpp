\
#include "arc.h"
#include "esphome/core/log.h"
#include <cctype>

namespace esphome {
namespace arc {

static const char *const TAG = "arc";

// ---------------- ARCCover ----------------

cover::CoverTraits ARCCover::get_traits() {
  cover::CoverTraits t;
  t.set_supports_position(true);
  t.set_supports_tilt(true);
  t.set_is_assumed_state(false);
  return t;
}

void ARCCover::control(const cover::CoverCall &call) {
  auto *bus = this->parent_;
  if (bus == nullptr) return;
  if (call.get_stop()) {
    bus->send_stop(this->address_);
  }
  if (call.get_position().has_value()) {
    float pos = *call.get_position();  // 0..1 (1=open)
    int pct = (int) roundf((1.0f - pos) * 100.0f); // ARC uses 0=open,100=closed for travel percent in many motors
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    bus->send_move_pct(this->address_, pct);
  }
  if (call.get_tilt().has_value()) {
    float tilt = *call.get_tilt(); // 0..1
    int deg = (int) roundf(tilt * 180.0f);
    if (deg < 0) deg = 0; if (deg > 180) deg = 180;
    bus->send_tilt_deg(this->address_, deg);
  }
  // Optimistically update state; real updates come from bus feedback
  this->publish_state();
}

void ARCCover::dump_config() {
  ESP_LOGCONFIG(TAG, "ARC Cover:");
  ESP_LOGCONFIG(TAG, "  Address: %s", this->address_.c_str());
}


// ---------------- ARCComponent ----------------

void ARCComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ARC bus...");
  this->set_rx_timeout(1); // minimal blocking
  // Register API services
  register_service(&ARCComponent::start_discovery, "arc_start_discovery");
  register_service(&ARCComponent::stop_discovery, "arc_stop_discovery");
  register_service(&ARCComponent::query_all, "arc_query_all");
  if (this->discovery_on_boot_) {
    this->start_discovery();
  }
}

void ARCComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "ARC:");
  ESP_LOGCONFIG(TAG, "  Discovery on boot: %s", YESNO(this->discovery_on_boot_));
  ESP_LOGCONFIG(TAG, "  Broadcast interval: %u ms", (unsigned) this->broadcast_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Idle gap: %u ms", (unsigned) this->idle_gap_ms_);
  ESP_LOGCONFIG(TAG, "  Devices discovered: %u", (unsigned) this->devices_.size());
  for (auto &kv : this->devices_) {
    const auto &d = kv.second;
    ESP_LOGCONFIG(TAG, "    %s ver=%s last_pos=%d last_tilt=%d last_seen=%u ms",
      d.addr.c_str(), d.version.c_str(), d.last_pos, d.last_tilt, (unsigned) d.last_seen_ms);
  }
}

void ARCComponent::loop() {
  // RX
  while (this->available()) {
    uint8_t b;
    this->read_byte(&b);
    this->on_byte(b);
  }
  // Discovery periodic broadcast
  uint32_t now = millis();
  if (this->discovery_) {
    if (now - this->last_broadcast_ms_ >= this->broadcast_interval_ms_) {
      this->query_all();
      this->last_broadcast_ms_ = now;
    }
  }
}

bool ARCComponent::bus_idle() const {
  uint32_t now = millis();
  return (now - this->last_byte_ms_) > this->idle_gap_ms_ && (now - this->last_tx_ms_) > this->idle_gap_ms_;
}

void ARCComponent::on_byte(uint8_t b) {
  this->last_byte_ms_ = millis();
  char c = (char) b;
  if (c == '!') {
    rx_buf_.clear();
    rx_buf_.push_back(c);
  } else if (!rx_buf_.empty()) {
    rx_buf_.push_back(c);
    if (c == ';') {
      // complete frame
      auto frame = rx_buf_;
      rx_buf_.clear();
      if (valid_frame_(frame)) {
        parse_frame_(frame);
      } else {
        ESP_LOGV(TAG, "Invalid frame: %s", frame.c_str());
      }
    } else if (rx_buf_.size() > 64) {
      // safety: don't allow unbounded growth
      ESP_LOGV(TAG, "Frame too long, resetting");
      rx_buf_.clear();
    }
  }
}

bool ARCComponent::valid_frame_(const std::string &frame) const {
  // Basic validation: starts with '!' ends with ';' and at least 4 chars
  if (frame.size() < 4) return false;
  if (frame.front() != '!' || frame.back() != ';') return false;
  return true;
}

// Expected examples:
// !111D123r050b090;
// !111D123U;
// !111D123vA21;
void ARCComponent::parse_frame_(const std::string &frame) {
  ESP_LOGV(TAG, "RX: %s", frame.c_str());

  // Try to extract address block between 'D' and command letter, or first 3 ASCII digits/letters after '!'
  std::string hub_or_prefix;
  std::string motor;
  size_t i = 1; // skip '!'
  // Gather up to 'D' if present
  size_t dpos = frame.find('D', 1);
  if (dpos != std::string::npos) {
    // format !<hub>D<motor><...>;
    hub_or_prefix = frame.substr(1, dpos - 1);
    // motor starts at dpos+1 until next non-alnum
    size_t mstart = dpos + 1;
    size_t mend = mstart;
    while (mend < frame.size() && std::isalnum((unsigned char) frame[mend])) mend++;
    motor = frame.substr(mstart, mend - mstart);
    // After mend is command char
    if (motor.empty()) return;
  } else {
    // Some fleets omit hub; try to read 3 alnums as motor
    size_t mstart = 1;
    size_t mend = mstart;
    while (mend < frame.size() && std::isalnum((unsigned char) frame[mend]) && (mend - mstart) < 6) mend++;
    motor = frame.substr(mstart, mend - mstart);
    if (motor.empty()) return;
  }

  // Extract command letter and optional data between it and ';'
  // Find first lowercase/uppercase letter after the motor id
  size_t cmd_pos = std::string::npos;
  for (size_t k = 1; k < frame.size(); ++k) {
    if (std::isalpha((unsigned char) frame[k])) { cmd_pos = k; break; }
  }
  if (cmd_pos == std::string::npos) return;
  char cmd = frame[cmd_pos];
  std::string data = frame.substr(cmd_pos + 1, frame.size() - cmd_pos - 2); // exclude trailing ';'

  // Update registry
  auto &dev = devices_[motor];
  dev.addr = motor;
  dev.last_seen_ms = millis();

  // Parse known responses
  // rDD1bDD2 => position + tilt
  if (cmd == 'r') {
    int pos = -1, tilt = -1;
    // find 'b'
    size_t bpos = data.find('b');
    if (bpos != std::string::npos) {
      std::string s1 = data.substr(0, bpos);
      std::string s2 = data.substr(bpos + 1);
      try {
        pos = std::stoi(s1);
        tilt = std::stoi(s2);
      } catch (...) {}
    }
    if (pos >= 0 && pos <= 100) dev.last_pos = pos;
    if (tilt >= 0 && tilt <= 180) dev.last_tilt = tilt;
  } else if (cmd == 'v') {
    // version like A21 or A(DD)
    dev.version = data;
  } else if (cmd == 'U') {
    // idle/unknown stroke
  } else if (cmd == '<') {
    // start move feedback: <DD1bDD2
  }

  // Push state into any bound covers
  for (auto *c : covers_) {
    if (c->get_address() == motor) {
      auto st = c->position;
      if (dev.last_pos >= 0) {
        // HA expects 1.0=open; ARC pos is 0=open 100=closed
        float open_frac = 1.0f - (float) dev.last_pos / 100.0f;
        c->position = open_frac;
      }
      if (dev.last_tilt >= 0) {
        float tilt_frac = (float) dev.last_tilt / 180.0f;
        c->tilt = tilt_frac;
      }
      c->publish_state();
    }
  }
}

void ARCComponent::send_cmd_(const std::string &addr, char cmd, const std::string &data) {
  if (!this->bus_idle()) {
    // small wait; rely on loop cadence
  }
  // If you're acting as hub, you can omit hub or use 000. We send !<addr><cmd><data>;
  std::string s = "!" + addr + std::string(1, cmd) + data + ";";
  send_raw_(s);
}

void ARCComponent::send_raw_(const std::string &s) {
  for (auto ch : s) this->write_byte(ch);
  this->flush();
  last_tx_ms_ = millis();
  ESP_LOGV(TAG, "TX: %s", s.c_str());
}

void ARCComponent::send_open(const std::string &addr) { send_cmd_(addr, 'o'); }
void ARCComponent::send_close(const std::string &addr) { send_cmd_(addr, 'c'); }
void ARCComponent::send_stop(const std::string &addr) { send_cmd_(addr, 's'); }

void ARCComponent::send_move_pct(const std::string &addr, int pct) {
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  char buf[8]; snprintf(buf, sizeof(buf), "%03d", pct);
  send_cmd_(addr, 'm', buf);
}

void ARCComponent::send_tilt_deg(const std::string &addr, int deg) {
  if (deg < 0) deg = 0; if (deg > 180) deg = 180;
  char buf[8]; snprintf(buf, sizeof(buf), "%03d", deg);
  send_cmd_(addr, 'b', buf);
}

void ARCComponent::register_cover(ARCCover *c) {
  this->covers_.push_back(c);
}

void ARCComponent::start_discovery() {
  if (!this->discovery_) {
    this->discovery_ = true;
    this->last_broadcast_ms_ = 0;
    ESP_LOGI(TAG, "ARC discovery started");
  }
}

void ARCComponent::stop_discovery() {
  if (this->discovery_) {
    this->discovery_ = false;
    ESP_LOGI(TAG, "ARC discovery stopped");
  }
}

void ARCComponent::query_all() {
  // Broadcast version query; devices/hubs respond with version
  // Spec: !000V?; but when acting as hub, many motors accept !<addr>v?; only.
  // We'll try two strategies:
  // 1) Broadcast version (classic spec): !000V?;
  // 2) If known addresses exist, poll each with v? and r?
  std::string q = "!000V?;";
  send_raw_(q);
  // Also poll known devices
  for (auto &kv : devices_) {
    auto addr = kv.first;
    send_cmd_(addr, 'v', "?");
    send_cmd_(addr, 'r', "?");
  }
}

}  // namespace arc
}  // namespace esphome

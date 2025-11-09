#include "arc_bridge.h"
#include "arc_cover.h"
#include "esphome/core/log.h"

#include <Arduino.h>
#include <cctype>
#include <cstdio>

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_bridge";

void ARCBridgeComponent::setup() {
  this->boot_millis_ = millis();
  this->startup_guard_cleared_ = false;
  ESP_LOGI(TAG, "ARCBridge setup complete (startup guard %u ms, auto-poll %s, interval %u ms)", STARTUP_GUARD_MS,
           (this->auto_poll_enabled_ && this->query_interval_ms_ > 0) ? "enabled" : "disabled",
           this->query_interval_ms_);
}

void ARCBridgeComponent::loop() {
  // clear startup guard after delay
  if (!this->startup_guard_cleared_ && millis() - this->boot_millis_ >= STARTUP_GUARD_MS) {
    this->startup_guard_cleared_ = true;
    ESP_LOGI(TAG, "Startup guard cleared for ARC bridge");
  }

  // read UART
  while (this->available()) {
    int c = this->read();
    if (c < 0) break;
    rx_buffer_.push_back(static_cast<char>(c));

    size_t start = rx_buffer_.find('!');
    size_t end = rx_buffer_.find(';', start);
    if (start != std::string::npos && end != std::string::npos && end > start) {
      std::string frame = rx_buffer_.substr(start, end - start + 1);
      rx_buffer_.erase(0, end + 1);
      this->handle_frame(frame);
    } else if (rx_buffer_.size() > 256) {
      rx_buffer_.clear();  // prevent runaway buffer
    }
  }

  // periodic position query
  const bool auto_poll_active =
      this->startup_guard_cleared_ && this->auto_poll_enabled_ && this->query_interval_ms_ > 0 && !covers_.empty();
  uint32_t now = millis();
  if (auto_poll_active && now - this->last_query_millis_ >= this->query_interval_ms_) {
    this->last_query_millis_ = now;
    if (this->query_index_ >= covers_.size()) this->query_index_ = 0;
    ARCCover *cvr = covers_[query_index_];
    if (cvr != nullptr) this->send_query(cvr->get_blind_id());
    this->query_index_ = (this->query_index_ + 1) % covers_.size();
  }
}

void ARCBridgeComponent::register_cover(const std::string &id, ARCCover *cover) {
  covers_.push_back(cover);
  ESP_LOGD(TAG, "Registered cover id='%s'", id.c_str());
}

void ARCBridgeComponent::send_simple_(const std::string &id, char command, const std::string &payload) {
  std::string frame = "!" + id + command + payload + ";";
  this->write_str(frame.c_str());
  ESP_LOGD(TAG, "TX -> %s", frame.c_str());
}

void ARCBridgeComponent::send_open(const std::string &id) { send_simple_(id, 'o'); }
void ARCBridgeComponent::send_close(const std::string &id) { send_simple_(id, 'c'); }
void ARCBridgeComponent::send_stop(const std::string &id) { send_simple_(id, 's'); }

void ARCBridgeComponent::send_move(const std::string &id, uint8_t percent) {
  if (percent > 100) percent = 100;
  char buf[4];
  snprintf(buf, sizeof(buf), "%03u", percent);
  send_simple_(id, 'm', buf);
}

void ARCBridgeComponent::send_query(const std::string &id) { send_simple_(id, 'r', "?"); }

void ARCBridgeComponent::handle_frame(const std::string &frame) {
  ESP_LOGD(TAG, "RX raw -> %s", frame.c_str());
  if (frame.size() < 5) return;  // too short
  parse_frame(frame);
}

// Helper: convert raw R value to dBm and % quality
static void decode_rssi(uint8_t raw, float &dbm, float &pct) {
  // Observed practical range: 0x50 (80) ≈ -100 dBm  →  0xA8 (168) ≈ -40 dBm
  if (raw < 0x50) raw = 0x50;
  if (raw > 0xA8) raw = 0xA8;

  dbm = -100.0f + ((raw - 0x50) / float(0xA8 - 0x50)) * 60.0f;
  pct = ((dbm + 100.0f) / 60.0f) * 100.0f;

  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
}

// -------------------------------------------------------------------------
// Main parser
void ARCBridgeComponent::parse_frame(const std::string &frame) {
  if (frame.size() < 5) return;
  std::string body = frame.substr(1, frame.size() - 2);

  std::string id = body.substr(0, 3);
  std::string rest = (body.size() > 3) ? body.substr(3) : "";

  int pos = -1;
  float dbm = NAN;
  float pct = NAN;
  bool enp = false;
  bool enl = false;

  // parse position r###
  size_t rpos = rest.find('r');
  if (rpos != std::string::npos)
    pos = std::atoi(rest.c_str() + rpos + 1);

  // parse RSSI R##
  size_t Rpos = rest.find('R');
  if (Rpos != std::string::npos && Rpos + 2 <= rest.size()) {
    std::string hex_str = rest.substr(Rpos + 1, 2);
    int raw_val = std::strtol(hex_str.c_str(), nullptr, 16);
    decode_rssi(raw_val, dbm, pct);
    ESP_LOGI(TAG, "[%s] R=%s -> %.1f dBm (%.1f%%)", id.c_str(), hex_str.c_str(), dbm, pct);
  }

  // state flags
  enp = (rest.find("Enp") != std::string::npos);
  enl = (rest.find("Enl") != std::string::npos);

  auto it_lq = lq_map_.find(id);
  auto it_status = status_map_.find(id);

  if (enl) {
    if (it_status != status_map_.end() && it_status->second)
      it_status->second->publish_state("unavailable");
    if (it_lq != lq_map_.end() && it_lq->second)
      it_lq->second->publish_state(NAN);
    ESP_LOGW(TAG, "[%s] Lost link -> Offline", id.c_str());
  }
  else if (enp) {
    if (it_status != status_map_.end() && it_status->second)
      it_status->second->publish_state("unavailable");
    if (it_lq != lq_map_.end() && it_lq->second)
      it_lq->second->publish_state(NAN);
    ESP_LOGW(TAG, "[%s] Not paired -> Link quality cleared", id.c_str());
  }
  else if (!std::isnan(dbm)) {
    if (it_lq != lq_map_.end() && it_lq->second)
      it_lq->second->publish_state(dbm);
    if (it_status != status_map_.end() && it_status->second)
      it_status->second->publish_state("Online");
    ESP_LOGI(TAG, "Matched cover id='%s' pos=%d RSSI=%.1fdBm (%.1f%%)", id.c_str(), pos, dbm, pct);
  }

  // cover updates
  for (auto *cv : covers_) {
    if (!cv) continue;
    if (cv->get_blind_id() == id) {
      const bool offline = (enl || enp);
      cv->set_available(!offline);

      if (offline) {
        cv->publish_unavailable();
        cv->publish_link_quality(NAN);
      } else {
        if (pos >= 0)
          cv->publish_raw_position(pos);
        if (!std::isnan(dbm))
          cv->publish_link_quality(dbm);
      }

      ESP_LOGI(TAG, "Matched cover id='%s' pos=%d RSSI=%.1fdBm (enp=%d enl=%d)", id.c_str(), pos, dbm, enp, enl);
      break;
    }
  }

  ESP_LOGD(TAG, "Parsed id=%s r=%d RSSI=%.1f enp=%d enl=%d", id.c_str(), pos, dbm, enp, enl);
}

void ARCBridgeComponent::map_lq_sensor(const std::string &id, sensor::Sensor *s) {
  lq_map_[id] = s;
}

void ARCBridgeComponent::map_status_sensor(const std::string &id, text_sensor::TextSensor *s) {
  status_map_[id] = s;
}

void ARCBridgeComponent::send_pair_command() {
  std::string frame = "!000&;";
  this->write_str(frame.c_str());
  ESP_LOGI(TAG, "TX -> %s (pairing command)", frame.c_str());
}


}  // namespace arc_bridge
}  // namespace esphome

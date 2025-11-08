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
  ESP_LOGI(TAG, "ARCBridge setup complete (startup guard %u ms)", STARTUP_GUARD_MS);
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
  if (millis() - this->last_query_millis_ >= QUERY_INTERVAL_MS && !covers_.empty()) {
    this->last_query_millis_ = millis();
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

void ARCBridgeComponent::parse_frame(const std::string &frame) {
  if (frame.size() < 5) return;
  std::string body = frame.substr(1, frame.size() - 2);

  // Blind ID = first 3 uppercase letters
  std::string id = body.substr(0, 3);
  std::string rest = (body.size() > 3) ? body.substr(3) : "";

  int pos = -1;
  int rssi = -1;
  bool enp = false;
  bool enl = false;

  // position r###
  size_t rpos = rest.find('r');
  if (rpos != std::string::npos)
    pos = std::atoi(rest.c_str() + rpos + 1);

  // RSSI R##
  size_t Rpos = rest.find('R');
  if (Rpos != std::string::npos && Rpos + 1 < rest.size()) {
    const char *s = rest.c_str() + Rpos + 1;
    rssi = std::strtol(s, nullptr, 16);
  }

  // markers
  if (rest.find("Enp") != std::string::npos) enp = true;
  if (rest.find("Enl") != std::string::npos) enl = true;

  // mapped sensors
  auto it_lq = lq_map_.find(id);
  auto it_status = status_map_.find(id);

  if (enl) {
    // 🔴 Lost link
    if (it_status != status_map_.end() && it_status->second)
      it_status->second->publish_state("Offline");
    if (it_lq != lq_map_.end() && it_lq->second)
      it_lq->second->publish_state(NAN);
    ESP_LOGW(TAG, "[%s] Lost link -> Offline", id.c_str());
  }
  else if (enp) {
    // ⚪ Not paired
    if (it_status != status_map_.end() && it_status->second)
      it_status->second->publish_state("Not paired");
    if (it_lq != lq_map_.end() && it_lq->second)
      it_lq->second->publish_state(NAN);
    ESP_LOGW(TAG, "[%s] Not paired -> Link quality cleared", id.c_str());
  }
  else if (rssi >= 0) {
    // 🟢 Online / Active
    float pct = (rssi / 255.0f) * 100.0f;
    if (it_lq != lq_map_.end() && it_lq->second)
      it_lq->second->publish_state(pct);
    if (it_status != status_map_.end() && it_status->second)
      it_status->second->publish_state("Online");
    ESP_LOGD(TAG, "[%s] Online -> Link quality %.1f%%", id.c_str(), pct);
  }

  // cover updates
  for (auto *cv : covers_) {
    if (!cv) continue;
    if (cv->get_blind_id() == id) {
      if (enl || enp)
        cv->set_available(false);
      else
        cv->set_available(true);

      if (pos >= 0)
        cv->publish_raw_position(pos);
      else if (enp || enl)
        cv->publish_unavailable();

      if (rssi >= 0) {
        float pct = (rssi / 255.0f) * 100.0f;
        cv->publish_link_quality(pct);
      } else if (enl) {
        cv->publish_link_quality(NAN);
      }

      ESP_LOGI(TAG, "Matched cover id='%s' pos=%d R=%d (enp=%d enl=%d)", id.c_str(), pos, rssi, enp, enl);
      break;
    }
  }

  ESP_LOGD(TAG, "Parsed id=%s r=%d R=%d enp=%d enl=%d", id.c_str(), pos, rssi, enp, enl);
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

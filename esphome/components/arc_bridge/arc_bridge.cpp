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
    if (cvr != nullptr) this->send_query(cvr->get_id());
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
  // strip leading ! and trailing ;
  std::string body = frame.substr(1, frame.size() - 2);
  size_t i = 0;
  while (i < body.size() && std::isupper(body[i])) ++i;
  std::string id = body.substr(0, i);
  std::string rest = (i < body.size()) ? body.substr(i) : "";

  int pos = -1, rssi = -1;
  bool enp = false, enl = false;

  size_t rpos = rest.find('r');
  if (rpos != std::string::npos) pos = std::atoi(rest.c_str() + rpos + 1);

  size_t Rpos = rest.find('R');
  if (Rpos != std::string::npos && Rpos + 1 < rest.size()) {
    const char *s = rest.c_str() + Rpos + 1;
    rssi = std::strtol(s, nullptr, 16);
  }

  if (rest.find("Enp") != std::string::npos) enp = true;
  if (rest.find("Enl") != std::string::npos) enl = true;

  // publish updates
  for (auto *cv : covers_) {
    if (cv && cv->get_blind_id() == id) {
      if (pos >= 0) cv->publish_raw_position(pos);
      break;
    }
  }

  // link quality sensor (optional)
  if (rssi >= 0) {
    auto it = lq_map_.find(id);
    if (it != lq_map_.end() && it->second)
      it->second->publish_state((255.0f - rssi) * 100.0f / 255.0f);
  }

  // status text sensor
  auto it2 = status_map_.find(id);
  if (it2 != status_map_.end() && it2->second) {
    std::string status;
    if (enp) status += "Enp ";
    if (enl) status += "Enl";
    if (!status.empty()) it2->second->publish_state(status);
  }

  ESP_LOGD(TAG, "Parsed id=%s r=%d R=%d", id.c_str(), pos, rssi);
}

void ARCBridgeComponent::map_lq_sensor(const std::string &id, sensor::Sensor *s) {
  lq_map_[id] = s;
}
void ARCBridgeComponent::map_status_sensor(const std::string &id, text_sensor::TextSensor *s) {
  status_map_[id] = s;
}

}  // namespace arc_bridge
}  // namespace esphome

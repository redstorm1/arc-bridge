#include "arc_bridge.h"
#include "arc_cover.h"
#include "esphome/core/log.h"

#include <Arduino.h>
#include <cctype>
#include <cstdio>
#include <algorithm>

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_bridge";

// =========================================================
//  TX QUEUE IMPLEMENTATION
// =========================================================

void ARCBridgeComponent::queue_tx(const std::string &frame) {
  tx_queue_.push_back(frame);
  ESP_LOGD(TAG, "Enqueued TX: %s (queue size=%u)", frame.c_str(),
           (unsigned)tx_queue_.size());
}

void ARCBridgeComponent::process_tx_queue_() {
  uint32_t now = millis();

  if (tx_queue_.empty())
    return;

  // enforce safe ARC timing
  if (now - last_tx_millis_ < TX_GAP_MS)
    return;

  std::string frame = tx_queue_.front();
  tx_queue_.pop_front();

  this->write_str(frame.c_str());
  last_tx_millis_ = now;

  ESP_LOGD(TAG, "TX -> %s (queued send)", frame.c_str());
}

// =========================================================
//  SETUP
// =========================================================

void ARCBridgeComponent::setup() {
  while (this->available())
    this->read();  // purge stale UART

  this->boot_millis_ = millis();
  this->startup_guard_cleared_ = false;

  ESP_LOGI(TAG,
           "ARCBridge setup (startup guard %u ms, auto-poll %s, interval %u ms)",
           STARTUP_GUARD_MS,
           (this->auto_poll_enabled_ && this->query_interval_ms_ > 0)
               ? "enabled"
               : "disabled",
           this->query_interval_ms_);
}

// =========================================================
//  LOOP
// =========================================================

void ARCBridgeComponent::loop() {
  uint32_t now = millis();

  // Startup guard
  if (!this->startup_guard_cleared_ &&
      now - this->boot_millis_ >= STARTUP_GUARD_MS) {
    this->startup_guard_cleared_ = true;
    ESP_LOGI(TAG, "Startup guard cleared");
  }

  // -----------------------------
  // UART RX
  // -----------------------------
  while (this->available()) {
    int c = this->read();
    if (c < 0)
      break;

    rx_buffer_.push_back(static_cast<char>(c));
    last_rx_millis_ = now;

    if (rx_buffer_.size() > 256) {
      rx_buffer_.clear();
      ESP_LOGW(TAG, "RX buffer overflow cleared");
      continue;
    }

    auto start_it = std::find(rx_buffer_.begin(), rx_buffer_.end(), '!');
    auto end_it   = std::find(rx_buffer_.begin(), rx_buffer_.end(), ';');

    if (start_it != rx_buffer_.end() &&
        end_it   != rx_buffer_.end() &&
        end_it > start_it) {

      std::string frame(start_it, end_it + 1);
      rx_buffer_.erase(rx_buffer_.begin(), end_it + 1);
      this->handle_frame(frame);
    }
  }

  // -----------------------------
  // AUTO POLL
  // -----------------------------
  bool quiet_due_to_motion =
      (now - this->last_motion_millis_) < MOVEMENT_QUIET_MS;

  bool auto_poll_active =
      this->startup_guard_cleared_ &&
      this->auto_poll_enabled_ &&
      this->query_interval_ms_ > 0 &&
      !covers_.empty() &&
      !quiet_due_to_motion;

  if (auto_poll_active &&
      now - this->last_query_millis_ >= this->query_interval_ms_) {
    this->last_query_millis_ = now;
    this->send_query("000");
  }

  // -----------------------------
  // TX WATCHDOG (movement-aware)
  // -----------------------------
  if (!this->tx_queue_.empty()) {
      uint32_t since_rx = now - this->last_rx_millis_;
      bool quiet_due_to_motion =
          (now - this->last_motion_millis_) < MOVEMENT_QUIET_MS;
  
      if (since_rx >= TX_WATCHDOG_MS) {
        ESP_LOGW(TAG,
                 "TX Watchdog: No RX for %u ms while TX pending -> clearing queue",
                 since_rx);
  
        this->tx_queue_.clear();
  
        if (!quiet_due_to_motion) {
          // Safe: STM32 is idle
          this->queue_tx("!000r?;");
        } else {
          // Unsafe: STM32 may still be busy with RF due to movement
          ESP_LOGW(TAG, "Watchdog: wake-up poll suppressed due to movement quiet-time");
        }
      }
  }

  // -----------------------------
  // TX QUEUE PROCESSING
  // -----------------------------
  process_tx_queue_();
}

// =========================================================
//  COVER REGISTRATION
// =========================================================

void ARCBridgeComponent::register_cover(const std::string &id,
                                        ARCCover *cover) {
  covers_.push_back(cover);
  ESP_LOGD(TAG, "Registered cover id='%s'", id.c_str());
}

// =========================================================
//  COMMAND SENDERS  (all use queue_tx())
// =========================================================

void ARCBridgeComponent::send_simple_(const std::string &id, char command,
                                      const std::string &payload) {
  std::string frame = "!" + id + command + payload + ";";
  queue_tx(frame);
  ESP_LOGD(TAG, "TX queued -> %s", frame.c_str());
}

void ARCBridgeComponent::send_open(const std::string &id) {
  last_motion_millis_ = millis();
  send_simple_(id, 'o');
}

void ARCBridgeComponent::send_close(const std::string &id) {
  last_motion_millis_ = millis();
  send_simple_(id, 'c');
}

void ARCBridgeComponent::send_stop(const std::string &id) {
  last_motion_millis_ = millis();
  send_simple_(id, 's');
}

void ARCBridgeComponent::send_move(const std::string &id, uint8_t percent) {
  if (percent > 100)
    percent = 100;

  last_motion_millis_ = millis();

  char buf[4];
  snprintf(buf, sizeof(buf), "%03u", percent);
  send_simple_(id, 'm', buf);
}

void ARCBridgeComponent::send_query(const std::string &id) {
  send_simple_(id, 'r', "?");
}

void ARCBridgeComponent::send_pair_command() {
  std::string frame = "!000&;";
  queue_tx(frame);
  ESP_LOGI(TAG, "TX queued -> %s (pairing)", frame.c_str());
}

void ARCBridgeComponent::send_raw_command(const std::string &cmd) {
  if (cmd.empty()) {
    ESP_LOGW(TAG, "send_raw_command: empty ignored");
    return;
  }

  std::string tx = cmd;

  if (tx.front() != '!')
    tx.insert(0, "!");
  if (tx.back() != ';')
    tx.push_back(';');

  queue_tx(tx);
  ESP_LOGI(TAG, "TX queued (raw) -> %s", tx.c_str());
}

// =========================================================
//  FRAME PARSING
// =========================================================

void ARCBridgeComponent::handle_frame(const std::string &frame) {
  ESP_LOGD(TAG, "RX raw -> %s", frame.c_str());
  if (frame.size() < 5)
    return;
  parse_frame(frame);
}

static void decode_rssi(uint8_t raw, float &dbm, float &pct) {
  dbm = (raw / 2.0f) - 130.0f;

  if (dbm < -120) dbm = -120;
  if (dbm > -20)  dbm = -20;

  pct = (dbm + 120.0f);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
}

void ARCBridgeComponent::parse_frame(const std::string &frame) {
  if (frame.size() < 5)
    return;

  std::string body = frame.substr(1, frame.size() - 2);
  std::string id   = body.substr(0, 3);
  std::string rest = (body.size() > 3) ? body.substr(3) : "";

  int pos = -1;
  float dbm = NAN;
  float pct = NAN;

  bool enp = (rest.find("Enp") != std::string::npos);
  bool enl = (rest.find("Enl") != std::string::npos);

  size_t rpos = rest.find('r');
  if (rpos != std::string::npos)
    pos = std::atoi(rest.c_str() + rpos + 1);

  size_t Rpos = rest.find('R');
  if (Rpos != std::string::npos && Rpos + 2 <= rest.size()) {
    std::string hex_str = rest.substr(Rpos + 1, 2);
    int raw_val = std::strtol(hex_str.c_str(), nullptr, 16);
    decode_rssi(raw_val, dbm, pct);
    ESP_LOGI(TAG, "[%s] R=%s -> %.1f dBm (%.1f%%)",
             id.c_str(), hex_str.c_str(), dbm, pct);
  }

  auto it_lq = lq_map_.find(id);
  auto it_status = status_map_.find(id);

  if (enl) {
    if (it_status->second) it_status->second->publish_state("unavailable");
    if (it_lq->second)     it_lq->second->publish_state(NAN);
    ESP_LOGW(TAG, "[%s] Lost link", id.c_str());
  }
  else if (enp) {
    if (it_status->second) it_status->second->publish_state("unavailable");
    if (it_lq->second)     it_lq->second->publish_state(NAN);
    ESP_LOGW(TAG, "[%s] Not paired", id.c_str());
  }
  else if (!std::isnan(dbm)) {
    if (it_lq->second)     it_lq->second->publish_state(dbm);
    if (it_status->second) it_status->second->publish_state("Online");
  }

  for (auto *cv : covers_) {
    if (cv && cv->get_blind_id() == id) {
      if (enl || enp) {
        cv->publish_raw_position(-1);
        ESP_LOGW(TAG, "[%s] Marked unavailable", id.c_str());
      } else {
        if (pos >= 0)       cv->publish_raw_position(pos);
        if (!std::isnan(dbm)) cv->publish_link_quality(dbm);
      }
      break;
    }
  }

  ESP_LOGD(TAG, "Parsed id=%s r=%d RSSI=%.1f", id.c_str(), pos, dbm);
}

// =========================================================
//  SENSOR MAPPING
// =========================================================

void ARCBridgeComponent::map_lq_sensor(const std::string &id,
                                       sensor::Sensor *s) {
  lq_map_[id] = s;
}

void ARCBridgeComponent::map_status_sensor(const std::string &id,
                                           text_sensor::TextSensor *s) {
  status_map_[id] = s;
}

}  // namespace arc_bridge
}  // namespace esphome

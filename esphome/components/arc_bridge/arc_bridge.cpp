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

void ARCBridgeComponent::queue_tx_front(const std::string &frame) {
  tx_queue_.push_front(frame);
  ESP_LOGD(TAG, "Enqueued TX (PRIORITY): %s (queue size=%u)", frame.c_str(),
           (unsigned)tx_queue_.size());
}

static bool is_poll_frame_(const std::string &f) {
  // Poll frames we generate:
  //   !IDr?;
  //   !IDpVc?;
  return (f.find("r?;")   != std::string::npos) ||
         (f.find("pVc?;") != std::string::npos);
}

void ARCBridgeComponent::drop_pending_polls_() {
  if (tx_queue_.empty())
    return;

  size_t before = tx_queue_.size();

  tx_queue_.erase(
      std::remove_if(tx_queue_.begin(), tx_queue_.end(),
                     [](const std::string &f) { return is_poll_frame_(f); }),
      tx_queue_.end());

  size_t dropped = before - tx_queue_.size();
  if (dropped > 0) {
    ESP_LOGD(TAG, "Dropped %u queued poll frames", (unsigned)dropped);
  }
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

  uint32_t now = millis();

  this->boot_millis_ = now;
  this->startup_guard_cleared_ = false;

  // Initialise timing so watchdog / quiet logic don't misfire at boot
  this->last_tx_millis_     = now;
  this->last_rx_millis_     = now;
  this->last_motion_millis_ = now;
  this->last_query_millis_  = now;

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

    // OLD: global query (unreliable)
    // this->send_query("000");

    // NEW: per-blind position queries !IDr?;
    ESP_LOGD(TAG, "Auto-poll: querying positions for all %u covers", (unsigned) covers_.size());
    for (auto *cv : covers_) {
      if (!cv)
        continue;
      const std::string &bid = cv->get_blind_id();
      if (bid.size() == 3) {
        this->send_query(bid);
        this->send_voltage_query(bid);  // NEW
      }
    }
  }

  // -----------------------------
  // TX QUEUE PROCESSING
  // -----------------------------
  process_tx_queue_();
  
  // -----------------------------
  // TX WATCHDOG (movement-aware)
  // -----------------------------
  if (!this->tx_queue_.empty()) {

    // Skip watchdog entirely until first TX occurs
    if (this->last_tx_millis_ == this->boot_millis_) {
      // First TX hasn't happened → watchdog off
      return;
    }

    // Use signed deltas so millis() rollover doesn't produce huge values.
    int32_t dt_rx = (int32_t) (now - this->last_rx_millis_);
    int32_t dt_tx = (int32_t) (now - this->last_tx_millis_);

    if (dt_rx < 0) dt_rx = 0;
    if (dt_tx < 0) dt_tx = 0;

    bool quiet_due_to_motion_wd =
        (now - this->last_motion_millis_) < MOVEMENT_QUIET_MS;

    if ((uint32_t) dt_rx >= TX_WATCHDOG_MS &&
        (uint32_t) dt_tx >= TX_WATCHDOG_MS) {

      ESP_LOGW(TAG,
               "TX Watchdog: No RX for %u ms (last TX %u ms ago) "
               "while TX pending -> clearing queue",
               (uint32_t) dt_rx, (uint32_t) dt_tx);

      this->tx_queue_.clear();

      if (!quiet_due_to_motion_wd) {
        ESP_LOGW(TAG, "Watchdog: sending per-blind wake-up queries");
        for (auto *cv : covers_) {
          if (!cv) continue;
          const std::string &bid = cv->get_blind_id();
          if (bid.size() == 3) {
            this->send_query(bid);
          }
        }
      } else {
        ESP_LOGW(TAG, "Watchdog: wake-up poll suppressed due to movement quiet-time");
      }
    }
  }


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
                                      const std::string &payload,
                                      bool priority) {
  std::string frame = "!" + id + command + payload + ";";

  if (priority) {
    queue_tx_front(frame);
    ESP_LOGD(TAG, "TX queued (priority) -> %s", frame.c_str());
  } else {
    queue_tx(frame);
    ESP_LOGD(TAG, "TX queued -> %s", frame.c_str());
  }
}

void ARCBridgeComponent::send_open(const std::string &id) {
  last_motion_millis_ = millis();
  drop_pending_polls_();
  send_simple_(id, 'o', "", true);
}

void ARCBridgeComponent::send_close(const std::string &id) {
  last_motion_millis_ = millis();
  drop_pending_polls_();
  send_simple_(id, 'c', "", true);
}

void ARCBridgeComponent::send_stop(const std::string &id) {
  last_motion_millis_ = millis();
  drop_pending_polls_();
  send_simple_(id, 's', "", true);
}

void ARCBridgeComponent::send_move(const std::string &id, uint8_t percent) {
  if (percent > 100)
    percent = 100;

  last_motion_millis_ = millis();
  drop_pending_polls_();
  char buf[4];
  snprintf(buf, sizeof(buf), "%03u", percent);
  send_simple_(id, 'm', buf, true);
}

void ARCBridgeComponent::send_query(const std::string &id) {
  send_simple_(id, 'r', "?", false);
}

// NEW: send !XXXpVc?;
void ARCBridgeComponent::send_voltage_query(const std::string &id) {
  // This builds: "!" + id + 'p' + "Vc?" + ";"
  send_simple_(id, 'p', "Vc?", false);
}

void ARCBridgeComponent::send_pair_command() {
  std::string frame = "!000&;";
  drop_pending_polls_();
  queue_tx_front(frame);
  ESP_LOGI(TAG, "TX queued (priority) -> %s (pairing)", frame.c_str());
}

void ARCBridgeComponent::send_raw_command(const std::string &cmd) {
  if (cmd.empty()) {
    ESP_LOGW(TAG, "send_raw_command: empty ignored");
    return;
  }

  std::string tx = cmd;
  if (tx.front() != '!') tx.insert(0, "!");
  if (tx.back()  != ';') tx.push_back(';');

  drop_pending_polls_();
  queue_tx_front(tx);
  ESP_LOGI(TAG, "TX queued (raw, priority) -> %s", tx.c_str());
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

  // NEW: handle pVc replies: look for "pVc" followed by digits
  {
    size_t pvc_pos = rest.find("pVc");
    if (pvc_pos != std::string::npos) {
      size_t vstart = pvc_pos + 3;  // right after "pVc"
      size_t vend   = vstart;
      while (vend < rest.size() &&
             std::isdigit(static_cast<unsigned char>(rest[vend]))) {
        vend++;
      }
      if (vend > vstart) {
        std::string digits = rest.substr(vstart, vend - vstart);
        this->handle_pvc_value_(id, digits);
      }
    }
  }

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
        if (pos >= 0)         cv->publish_raw_position(pos);
        if (!std::isnan(dbm)) cv->publish_link_quality(dbm);
      }
      break;
    }
  }

  ESP_LOGD(TAG, "Parsed id=%s r=%d RSSI=%.1f", id.c_str(), pos, dbm);
}

void ARCBridgeComponent::handle_pvc_value_(const std::string &id, const std::string &digits) {
  // Parse integer without exceptions
  char *endptr = nullptr;
  long raw_val = std::strtol(digits.c_str(), &endptr, 10);
  if (endptr == digits.c_str() || raw_val < 0) {
    ESP_LOGW(TAG, "[%s] Invalid pVc digits='%s'", id.c_str(), digits.c_str());
    return;
  }

  int raw = static_cast<int>(raw_val);

  auto it = voltage_map_.find(id);
  if (it == voltage_map_.end() || !it->second) {
    ESP_LOGD(TAG, "[%s] pVc=%d but no mapped voltage sensor", id.c_str(), raw);
    return;
  }

  // 0 → AC motor; publish 0.0V but log as AC
  if (raw == 0) {
    it->second->publish_state(0.0f);
    ESP_LOGD(TAG, "[%s] pVc=0 -> AC motor, publishing 0.00V", id.c_str());
    return;
  }

  // Non-zero → scaled voltage (raw is in centivolts)
  float v = raw / 100.0f;

  it->second->publish_state(v);

  ESP_LOGD(TAG, "[%s] pVc raw=%s -> %.2fV", id.c_str(), digits.c_str(), v);
}



// =========================================================
//  SENSOR MAPPING
// =========================================================

void ARCBridgeComponent::map_lq_sensor(const std::string &id,
                                       sensor::Sensor *s) {
  lq_map_[id] = s;
}

void ARCBridgeComponent::map_status_sensor(const std::string &id,text_sensor::TextSensor *s) {
  status_map_[id] = s;
}

// NEW: voltage text sensor mapping
void ARCBridgeComponent::map_voltage_sensor(const std::string &id, sensor::Sensor *s) {
  voltage_map_[id] = s;
  ESP_LOGD(TAG, "Mapped voltage sensor for id='%s'", id.c_str());
}
}  // namespace arc_bridge
}  // namespace esphome

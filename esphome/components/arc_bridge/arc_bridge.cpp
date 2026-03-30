#include "arc_bridge.h"

#include "battery.h"
#include "arc_cover.h"
#include "protocol.h"
#include "tx_queue.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_bridge";

namespace {

template<typename T>
T *find_mapped_(std::unordered_map<std::string, T *> &map, const std::string &id) {
  auto it = map.find(id);
  return (it != map.end()) ? it->second : nullptr;
}

static void decode_rssi(uint8_t raw, float &dbm, float &pct) {
  dbm = (raw / 2.0f) - 130.0f;

  if (dbm < -120) {
    dbm = -120;
  }
  if (dbm > -20) {
    dbm = -20;
  }

  pct = dbm + 120.0f;
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 100) {
    pct = 100;
  }
}

static std::string format_version_text_(const ParsedFrame &parsed) {
  if (!parsed.motor_type_code.has_value() || !parsed.version_code.has_value()) {
    return "";
  }

  std::string type_name;
  switch (*parsed.motor_type_code) {
    case 'A':
      type_name = "AC";
      break;
    case 'C':
      type_name = "Curtain";
      break;
    case 'D':
      type_name = "DC";
      break;
    case 'S':
      type_name = "Socket";
      break;
    case 'L':
      type_name = "Lighting";
      break;
    default:
      type_name = std::string("Type ") + *parsed.motor_type_code;
      break;
  }

  if (parsed.version_major.has_value() && parsed.version_minor.has_value()) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%s v%d.%d", type_name.c_str(),
             *parsed.version_major, *parsed.version_minor);
    return buffer;
  }

  return type_name + " " + *parsed.version_code;
}

static std::string format_limits_text_(const std::string &code) {
  if (code == "00") {
    return "Unset";
  }
  if (code == "01") {
    return "Upper/Lower Set";
  }
  if (code == "03") {
    return "Upper/Lower/Preferred Set";
  }
  return "Code " + code;
}

}  // namespace

void ARCBridgeComponent::queue_tx(const std::string &frame,
                                  TxPacingClass pacing_class,
                                  bool is_poll) {
  this->tx_queue_.push_back({frame, pacing_class, is_poll});
  ESP_LOGD(TAG, "Enqueued TX: %s (queue size=%u, gap=%u ms)", frame.c_str(),
           (unsigned) this->tx_queue_.size(), tx_gap_ms_for(pacing_class));
}

void ARCBridgeComponent::queue_tx_front(const std::string &frame,
                                        TxPacingClass pacing_class,
                                        bool is_poll) {
  this->tx_queue_.push_front({frame, pacing_class, is_poll});
  ESP_LOGD(TAG, "Enqueued TX (priority): %s (queue size=%u, gap=%u ms)", frame.c_str(),
           (unsigned) this->tx_queue_.size(), tx_gap_ms_for(pacing_class));
}

void ARCBridgeComponent::drop_pending_polls_() {
  if (this->tx_queue_.empty()) {
    return;
  }

  const size_t before = this->tx_queue_.size();
  drop_pending_poll_items(this->tx_queue_);

  const size_t dropped = before - this->tx_queue_.size();
  if (dropped > 0) {
    ESP_LOGD(TAG, "Dropped %u queued poll frames", (unsigned) dropped);
  }
}

void ARCBridgeComponent::process_tx_queue_() {
  const uint32_t now = millis();

  if (this->tx_queue_.empty()) {
    return;
  }

  const TxQueueItem &item = this->tx_queue_.front();
  const uint32_t required_gap = tx_gap_ms_for(item.pacing_class);
  if (now - this->last_tx_millis_ < required_gap) {
    return;
  }

  const std::string frame = item.frame;
  this->tx_queue_.pop_front();

  this->write_str(frame.c_str());
  this->last_tx_millis_ = now;

  ESP_LOGD(TAG, "TX -> %s (queued send, gap=%u ms)", frame.c_str(), required_gap);
}

void ARCBridgeComponent::setup() {
  while (this->available()) {
    this->read();
  }

  const uint32_t now = millis();

  this->boot_millis_ = now;
  this->startup_guard_cleared_ = false;
  this->last_tx_millis_ = now;
  this->last_rx_millis_ = now;
  this->last_motion_millis_ = now;
  this->last_query_millis_ = now;
  this->query_index_ = 0;

  ESP_LOGI(TAG,
           "ARCBridge setup (startup guard %u ms, auto-poll %s, interval %u ms, tx gaps default=%u ms motion=%u ms)",
           STARTUP_GUARD_MS,
           (this->auto_poll_enabled_ && this->query_interval_ms_ > 0) ? "enabled" : "disabled",
           this->query_interval_ms_,
           tx_gap_ms_for(TxPacingClass::STANDARD),
           tx_gap_ms_for(TxPacingClass::MOTION));
}

void ARCBridgeComponent::loop() {
  const uint32_t now = millis();

  if (!this->startup_guard_cleared_ && now - this->boot_millis_ >= STARTUP_GUARD_MS) {
    this->startup_guard_cleared_ = true;
    ESP_LOGI(TAG, "Startup guard cleared");
  }

  while (this->available()) {
    const int c = this->read();
    if (c < 0) {
      break;
    }

    this->rx_buffer_.push_back(static_cast<char>(c));
    this->last_rx_millis_ = now;

    if (this->rx_buffer_.size() > 256) {
      this->rx_buffer_.clear();
      ESP_LOGW(TAG, "RX buffer overflow cleared");
      continue;
    }

    auto start_it = std::find(this->rx_buffer_.begin(), this->rx_buffer_.end(), '!');
    auto end_it = std::find(this->rx_buffer_.begin(), this->rx_buffer_.end(), ';');

    if (start_it != this->rx_buffer_.end() && end_it != this->rx_buffer_.end() && end_it > start_it) {
      const std::string frame(start_it, end_it + 1);
      this->rx_buffer_.erase(this->rx_buffer_.begin(), end_it + 1);
      this->handle_frame(frame);
    }
  }

  const bool quiet_due_to_motion = (now - this->last_motion_millis_) < MOVEMENT_QUIET_MS;
  const bool auto_poll_active = this->startup_guard_cleared_ && this->auto_poll_enabled_ &&
                                this->query_interval_ms_ > 0 && !this->covers_.empty() &&
                                !quiet_due_to_motion;

  if (auto_poll_active && now - this->last_query_millis_ >= this->query_interval_ms_) {
    this->last_query_millis_ = now;

    size_t attempts = this->covers_.size();
    while (attempts-- > 0) {
      if (this->query_index_ >= this->covers_.size()) {
        this->query_index_ = 0;
      }

      ARCCover *cover = this->covers_[this->query_index_++];
      if (cover == nullptr) {
        continue;
      }

      const std::string &blind_id = cover->get_blind_id();
      if (blind_id.size() != 3) {
        continue;
      }

      ESP_LOGD(TAG, "Auto-poll: querying blind %s", blind_id.c_str());
      this->enqueue_queries_for_id_(blind_id, false);
      break;
    }
  }

  this->process_tx_queue_();

  if (this->tx_queue_.empty()) {
    return;
  }

  if (this->last_tx_millis_ == this->boot_millis_) {
    return;
  }

  int32_t dt_rx = static_cast<int32_t>(now - this->last_rx_millis_);
  int32_t dt_tx = static_cast<int32_t>(now - this->last_tx_millis_);
  if (dt_rx < 0) {
    dt_rx = 0;
  }
  if (dt_tx < 0) {
    dt_tx = 0;
  }

  if (static_cast<uint32_t>(dt_rx) >= TX_WATCHDOG_MS &&
      static_cast<uint32_t>(dt_tx) >= TX_WATCHDOG_MS) {
    ESP_LOGW(TAG,
             "TX watchdog: no RX for %u ms (last TX %u ms ago) while TX pending -> clearing queue",
             (uint32_t) dt_rx, (uint32_t) dt_tx);

    this->tx_queue_.clear();

    if (!quiet_due_to_motion && !this->covers_.empty()) {
      if (this->query_index_ >= this->covers_.size()) {
        this->query_index_ = 0;
      }

      ARCCover *cover = this->covers_[this->query_index_];
      if (cover != nullptr) {
        const std::string &blind_id = cover->get_blind_id();
        if (blind_id.size() == 3) {
          ESP_LOGW(TAG, "Watchdog: sending wake-up query to %s", blind_id.c_str());
          this->enqueue_queries_for_id_(blind_id, false);
        }
      }
    } else if (quiet_due_to_motion) {
      ESP_LOGW(TAG, "Watchdog: wake-up poll suppressed due to movement quiet-time");
    }
  }
}

void ARCBridgeComponent::register_cover(const std::string &id, ARCCover *cover) {
  this->covers_.push_back(cover);

  if (this->cover_map_.count(id) != 0) {
    ESP_LOGW(TAG, "Duplicate cover registration for id='%s' will replace direct lookup", id.c_str());
  }
  this->cover_map_[id] = cover;

  ESP_LOGD(TAG, "Registered cover id='%s'", id.c_str());
}

void ARCBridgeComponent::send_simple_(const std::string &id, char command,
                                      const std::string &payload, bool priority,
                                      TxPacingClass pacing_class, bool is_poll) {
  const std::string frame = "!" + id + command + payload + ";";

  if (priority) {
    this->queue_tx_front(frame, pacing_class, is_poll);
    ESP_LOGD(TAG, "TX queued (priority) -> %s", frame.c_str());
  } else {
    this->queue_tx(frame, pacing_class, is_poll);
    ESP_LOGD(TAG, "TX queued -> %s", frame.c_str());
  }
}

void ARCBridgeComponent::send_open(const std::string &id) {
  this->last_motion_millis_ = millis();
  this->drop_pending_polls_();
  this->send_simple_(id, 'o', "", true, TxPacingClass::MOTION, false);
}

void ARCBridgeComponent::send_close(const std::string &id) {
  this->last_motion_millis_ = millis();
  this->drop_pending_polls_();
  this->send_simple_(id, 'c', "", true, TxPacingClass::MOTION, false);
}

void ARCBridgeComponent::send_stop(const std::string &id) {
  this->last_motion_millis_ = millis();
  this->drop_pending_polls_();
  this->send_simple_(id, 's', "", true, TxPacingClass::MOTION, false);
}

void ARCBridgeComponent::send_move(const std::string &id, uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }

  this->last_motion_millis_ = millis();
  this->drop_pending_polls_();

  char buffer[4];
  snprintf(buffer, sizeof(buffer), "%03u", percent);
  this->send_simple_(id, 'm', buffer, true, TxPacingClass::MOTION, false);
}

void ARCBridgeComponent::send_query(const std::string &id) {
  this->send_simple_(id, 'r', "?", false, TxPacingClass::STANDARD, true);
}

void ARCBridgeComponent::send_query_all() {
  this->drop_pending_polls_();

  ESP_LOGI(TAG, "Queueing a manual query pass for %u covers", (unsigned) this->covers_.size());
  for (auto *cover : this->covers_) {
    if (cover == nullptr) {
      continue;
    }
    const std::string &blind_id = cover->get_blind_id();
    if (blind_id.size() != 3) {
      continue;
    }
    this->enqueue_queries_for_id_(blind_id, true);
  }
}

void ARCBridgeComponent::send_pair_command() {
  this->drop_pending_polls_();
  this->queue_tx_front("!000&;", TxPacingClass::STANDARD, false);
  ESP_LOGI(TAG, "TX queued (priority) -> !000&; (pairing)");
}

void ARCBridgeComponent::send_raw_command(const std::string &cmd) {
  if (cmd.empty()) {
    ESP_LOGW(TAG, "send_raw_command: empty ignored");
    return;
  }

  std::string tx = cmd;
  if (tx.front() != '!') {
    tx.insert(0, "!");
  }
  if (tx.back() != ';') {
    tx.push_back(';');
  }

  this->drop_pending_polls_();
  this->queue_tx_front(tx, TxPacingClass::STANDARD, false);
  ESP_LOGI(TAG, "TX queued (raw, priority) -> %s", tx.c_str());
}

void ARCBridgeComponent::send_favorite(const std::string &id) {
  this->last_motion_millis_ = millis();
  this->drop_pending_polls_();
  this->send_simple_(id, 'f', "", true, TxPacingClass::MOTION, false);
}

void ARCBridgeComponent::send_jog_open(const std::string &id) {
  this->last_motion_millis_ = millis();
  this->drop_pending_polls_();
  this->send_simple_(id, 'o', "A", true, TxPacingClass::MOTION, false);
}

void ARCBridgeComponent::send_jog_close(const std::string &id) {
  this->last_motion_millis_ = millis();
  this->drop_pending_polls_();
  this->send_simple_(id, 'c', "A", true, TxPacingClass::MOTION, false);
}

void ARCBridgeComponent::send_voltage_query(const std::string &id) {
  this->send_simple_(id, 'p', "Vc?", false, TxPacingClass::STANDARD, true);
}

void ARCBridgeComponent::send_version_query(const std::string &id) {
  this->send_simple_(id, 'v', "?", false, TxPacingClass::STANDARD, true);
}

void ARCBridgeComponent::send_speed_query(const std::string &id) {
  this->send_simple_(id, 'p', "Sc?", false, TxPacingClass::STANDARD, true);
}

void ARCBridgeComponent::send_limits_query(const std::string &id) {
  this->send_simple_(id, 'p', "P?", false, TxPacingClass::STANDARD, true);
}

void ARCBridgeComponent::enqueue_queries_for_id_(const std::string &id, bool force_static) {
  this->send_query(id);

  if (find_mapped_(this->voltage_map_, id) != nullptr ||
      find_mapped_(this->battery_level_map_, id) != nullptr) {
    this->send_voltage_query(id);
  }

  if (find_mapped_(this->speed_map_, id) != nullptr) {
    this->send_speed_query(id);
  }

  if (auto *version_sensor = find_mapped_(this->version_map_, id);
      version_sensor != nullptr && (force_static || !version_sensor->has_state())) {
    this->send_version_query(id);
  }

  if (auto *limits_sensor = find_mapped_(this->limits_map_, id);
      limits_sensor != nullptr && (force_static || !limits_sensor->has_state())) {
    this->send_limits_query(id);
  }
}

void ARCBridgeComponent::handle_frame(const std::string &frame) {
  ESP_LOGD(TAG, "RX raw -> %s", frame.c_str());
  if (frame.size() < 5) {
    return;
  }
  this->parse_frame(frame);
}

void ARCBridgeComponent::parse_frame(const std::string &frame) {
  const ParsedFrame parsed = parse_arc_frame(frame);
  if (!parsed.valid) {
    return;
  }

  const std::string &id = parsed.id;
  auto *cover = find_mapped_(this->cover_map_, id);
  auto *lq_sensor = find_mapped_(this->lq_map_, id);
  auto *status_sensor = find_mapped_(this->status_map_, id);
  auto *version_sensor = find_mapped_(this->version_map_, id);
  auto *speed_sensor = find_mapped_(this->speed_map_, id);
  auto *limits_sensor = find_mapped_(this->limits_map_, id);

  float dbm = NAN;
  float pct = NAN;
  if (parsed.rssi_raw.has_value()) {
    decode_rssi(static_cast<uint8_t>(*parsed.rssi_raw), dbm, pct);
    ESP_LOGI(TAG, "[%s] R=%02X -> %.1f dBm (%.1f%%)", id.c_str(),
             *parsed.rssi_raw, dbm, pct);
  }

  if (parsed.voltage_centivolts.has_value()) {
    this->handle_pvc_value_(id, std::to_string(*parsed.voltage_centivolts));
  }

  if (parsed.speed_rpm.has_value() && speed_sensor != nullptr) {
    speed_sensor->publish_state(static_cast<float>(*parsed.speed_rpm));
    ESP_LOGD(TAG, "[%s] speed=%d rpm", id.c_str(), *parsed.speed_rpm);
  }

  if (parsed.version_code.has_value() && version_sensor != nullptr) {
    const std::string version_text = format_version_text_(parsed);
    version_sensor->publish_state(version_text.empty() ? *parsed.version_code : version_text);
    ESP_LOGD(TAG, "[%s] version=%s", id.c_str(),
             version_text.empty() ? parsed.version_code->c_str() : version_text.c_str());
  }

  if (parsed.limits_code.has_value() && limits_sensor != nullptr) {
    const std::string limits_text = format_limits_text_(*parsed.limits_code);
    limits_sensor->publish_state(limits_text);
    ESP_LOGD(TAG, "[%s] limits=%s", id.c_str(), limits_text.c_str());
  }

  if (parsed.lost_link) {
    if (status_sensor != nullptr) {
      status_sensor->publish_state("Offline");
    }
    if (lq_sensor != nullptr) {
      lq_sensor->publish_state(NAN);
    }
    if (cover != nullptr) {
      cover->set_available(false);
    }
    ESP_LOGW(TAG, "[%s] Lost link", id.c_str());
    return;
  }

  if (parsed.not_paired) {
    if (status_sensor != nullptr) {
      status_sensor->publish_state("Not Paired");
    }
    if (lq_sensor != nullptr) {
      lq_sensor->publish_state(NAN);
    }
    if (cover != nullptr) {
      cover->set_available(false);
    }
    ESP_LOGW(TAG, "[%s] Not paired", id.c_str());
    return;
  }

  if (!std::isnan(dbm) && lq_sensor != nullptr) {
    lq_sensor->publish_state(dbm);
  }

  if (status_sensor != nullptr) {
    if (parsed.no_position) {
      status_sensor->publish_state("No Position");
    } else {
      status_sensor->publish_state("Online");
    }
  }

  if (cover != nullptr && !cover->has_state()) {
    cover->set_available(true);
  }

  if (parsed.position_percent.has_value() && cover != nullptr) {
    cover->publish_raw_position(*parsed.position_percent);
    if (parsed.position_in_motion) {
      ESP_LOGD(TAG, "[%s] In-progress position=%d", id.c_str(), *parsed.position_percent);
    }
  }

  if (parsed.no_position) {
    ESP_LOGW(TAG, "[%s] No position/limits feedback", id.c_str());
  }

  ESP_LOGD(TAG, "Parsed id=%s pos=%d moving=%s RSSI=%.1f",
           id.c_str(),
           parsed.position_percent.value_or(-1),
           parsed.position_in_motion ? "true" : "false",
           dbm);
}

void ARCBridgeComponent::handle_pvc_value_(const std::string &id, const std::string &digits) {
  char *endptr = nullptr;
  const long raw_value = std::strtol(digits.c_str(), &endptr, 10);
  if (endptr == digits.c_str() || raw_value < 0) {
    ESP_LOGW(TAG, "[%s] Invalid pVc digits='%s'", id.c_str(), digits.c_str());
    return;
  }

  auto *sensor = find_mapped_(this->voltage_map_, id);
  auto *battery_sensor = find_mapped_(this->battery_level_map_, id);
  if (sensor == nullptr && battery_sensor == nullptr) {
    ESP_LOGD(TAG, "[%s] pVc=%ld but no mapped voltage or battery sensor", id.c_str(), raw_value);
    return;
  }

  if (raw_value == 0) {
    if (sensor != nullptr) {
      sensor->publish_state(0.0f);
    }
    if (battery_sensor != nullptr) {
      battery_sensor->publish_state(NAN);
    }
    ESP_LOGD(TAG, "[%s] pVc=0 -> AC motor, publishing 0.00V and leaving battery unavailable",
             id.c_str());
    return;
  }

  const float volts = static_cast<float>(raw_value) / 100.0f;
  if (sensor != nullptr) {
    sensor->publish_state(volts);
  }
  if (battery_sensor != nullptr) {
    const float battery_pct = battery_percent_from_3s_li_ion(volts);
    battery_sensor->publish_state(battery_pct);
    ESP_LOGD(TAG, "[%s] pVc raw=%s -> %.2fV / %.1f%%", id.c_str(), digits.c_str(), volts,
             battery_pct);
  } else {
    ESP_LOGD(TAG, "[%s] pVc raw=%s -> %.2fV", id.c_str(), digits.c_str(), volts);
  }
}

void ARCBridgeComponent::map_lq_sensor(const std::string &id, sensor::Sensor *sensor) {
  this->lq_map_[id] = sensor;
}

void ARCBridgeComponent::map_status_sensor(const std::string &id, text_sensor::TextSensor *sensor) {
  this->status_map_[id] = sensor;
}

void ARCBridgeComponent::map_voltage_sensor(const std::string &id, sensor::Sensor *sensor) {
  this->voltage_map_[id] = sensor;
  ESP_LOGD(TAG, "Mapped voltage sensor for id='%s'", id.c_str());
}

void ARCBridgeComponent::map_battery_level_sensor(const std::string &id, sensor::Sensor *sensor) {
  this->battery_level_map_[id] = sensor;
  ESP_LOGD(TAG, "Mapped battery level sensor for id='%s'", id.c_str());
}

void ARCBridgeComponent::map_version_sensor(const std::string &id, text_sensor::TextSensor *sensor) {
  this->version_map_[id] = sensor;
  ESP_LOGD(TAG, "Mapped version sensor for id='%s'", id.c_str());
}

void ARCBridgeComponent::map_speed_sensor(const std::string &id, sensor::Sensor *sensor) {
  this->speed_map_[id] = sensor;
  ESP_LOGD(TAG, "Mapped speed sensor for id='%s'", id.c_str());
}

void ARCBridgeComponent::map_limits_sensor(const std::string &id, text_sensor::TextSensor *sensor) {
  this->limits_map_[id] = sensor;
  ESP_LOGD(TAG, "Mapped limits sensor for id='%s'", id.c_str());
}

}  // namespace arc_bridge
}  // namespace esphome

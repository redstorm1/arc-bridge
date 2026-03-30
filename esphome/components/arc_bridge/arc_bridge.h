#pragma once

#include "delivery.h"
#include "tx_queue.h"

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <deque>

namespace esphome {
namespace arc_bridge {

class ARCCover;  // forward declaration

class ARCBridgeComponent : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;

  // registration
  void register_cover(const std::string &id, ARCCover *cover);

  // command API
  void send_open(const std::string &id);
  void send_close(const std::string &id);
  void send_stop(const std::string &id);
  void send_move(const std::string &id, uint8_t percent);
  void send_query(const std::string &id);
  void send_query_all();
  void send_pair_command();
  void send_raw_command(const std::string &cmd);
  void send_favorite(const std::string &id);
  void send_jog_open(const std::string &id);
  void send_jog_close(const std::string &id);

  void send_voltage_query(const std::string &id);
  void send_version_query(const std::string &id);
  void send_speed_query(const std::string &id);
  void send_limits_query(const std::string &id);

  // sensor mapping
  void map_lq_sensor(const std::string &id, sensor::Sensor *s);
  void map_status_sensor(const std::string &id, text_sensor::TextSensor *s);
  void map_voltage_sensor(const std::string &id, sensor::Sensor *s);
  void map_battery_level_sensor(const std::string &id, sensor::Sensor *s);
  void map_version_sensor(const std::string &id, text_sensor::TextSensor *s);
  void map_speed_sensor(const std::string &id, sensor::Sensor *s);
  void map_limits_sensor(const std::string &id, text_sensor::TextSensor *s);

  void set_auto_poll_enabled(bool enabled) { this->auto_poll_enabled_ = enabled; }
  void set_auto_poll_interval(uint32_t interval_ms) { this->query_interval_ms_ = interval_ms; }
  void set_command_retry_count(uint8_t retry_count) { this->command_retry_count_ = retry_count; }
  void set_command_retry_timeout(uint32_t timeout_ms) { this->command_retry_timeout_ms_ = timeout_ms; }
  void set_motion_tx_gap(uint32_t gap_ms) { this->motion_tx_gap_ms_ = gap_ms; }

  bool is_startup_guard_cleared() const { return this->startup_guard_cleared_; }

  void send_simple(const std::string &id, char cmd, const std::string &arg = "") {
    this->send_simple_(id, cmd, arg);
  }

 protected:
  void handle_frame(const std::string &frame);
  void parse_frame(const std::string &frame);
  void send_simple_(const std::string &id, char command, const std::string &payload = "",
                    bool priority = false,
                    TxPacingClass pacing_class = TxPacingClass::STANDARD,
                    bool is_poll = false,
                    DeliveryExpectation delivery_expectation = DeliveryExpectation::NONE,
                    bool allow_retry = false);
  void enqueue_queries_for_id_(const std::string &id, bool force_static);
  void handle_pvc_value_(const std::string &id, const std::string &digits);
  uint32_t allocate_tracking_id_();
  void arm_pending_delivery_(const TxQueueItem &item, uint32_t now);
  void acknowledge_pending_delivery_(const ParsedFrame &parsed);
  void process_pending_deliveries_();
  void send_verification_query_(const std::string &id);

  // ===============================
  // CONSTANTS (Option A ordering)
  // ===============================
  static const uint32_t QUERY_INTERVAL_MS = 10000;      // 10 seconds
  static const uint32_t STARTUP_GUARD_MS  = 10000;      // 10 seconds
  static const uint32_t MOVEMENT_QUIET_MS = 90000;      // 90 seconds
  static const uint32_t TX_WATCHDOG_MS    = 5000;       // 5 seconds
  static const uint8_t COMMAND_RETRY_COUNT = 1;         // one resend after verification
  static const uint32_t COMMAND_RETRY_TIMEOUT_MS = 1500;  // wait before verify/retry

  // ===============================
  // INTERNAL STATE
  // ===============================
  std::deque<char> rx_buffer_;
  uint32_t boot_millis_{0};
  uint32_t last_query_millis_{0};
  uint32_t last_rx_millis_{0};
  uint32_t last_motion_millis_{0};
  size_t query_index_{0};
  bool startup_guard_cleared_{false};
  bool auto_poll_enabled_{true};
  uint32_t query_interval_ms_{QUERY_INTERVAL_MS};
  uint8_t command_retry_count_{COMMAND_RETRY_COUNT};
  uint32_t command_retry_timeout_ms_{COMMAND_RETRY_TIMEOUT_MS};
  uint32_t motion_tx_gap_ms_{DEFAULT_MOTION_TX_GAP_MS};

  std::vector<ARCCover *> covers_;
  std::unordered_map<std::string, ARCCover *> cover_map_;
  std::unordered_map<std::string, sensor::Sensor *> lq_map_;
  std::unordered_map<std::string, text_sensor::TextSensor *> status_map_;
  std::unordered_map<std::string, sensor::Sensor *> voltage_map_;
  std::unordered_map<std::string, sensor::Sensor *> battery_level_map_;
  std::unordered_map<std::string, text_sensor::TextSensor *> version_map_;
  std::unordered_map<std::string, sensor::Sensor *> speed_map_;
  std::unordered_map<std::string, text_sensor::TextSensor *> limits_map_;
  struct PendingCommandDelivery {
    TxQueueItem item;
    uint8_t retries_used{0};
    uint32_t last_activity_ms{0};
    bool verification_sent{false};
  };
  std::unordered_map<std::string, PendingCommandDelivery> pending_command_deliveries_;
  uint32_t next_tracking_id_{1};

  // ===============================
  // TX QUEUE SUPPORT
  // ===============================
  std::deque<TxQueueItem> tx_queue_;
  uint32_t last_tx_millis_{0};
  void queue_tx(const std::string &frame,
                TxPacingClass pacing_class = TxPacingClass::STANDARD,
                bool is_poll = false,
                const std::string &blind_id = "",
                DeliveryExpectation delivery_expectation = DeliveryExpectation::NONE,
                bool allow_retry = false,
                uint32_t tracking_id = 0);
  void queue_tx_front(const std::string &frame,
                      TxPacingClass pacing_class = TxPacingClass::STANDARD,
                      bool is_poll = false,
                      const std::string &blind_id = "",
                      DeliveryExpectation delivery_expectation = DeliveryExpectation::NONE,
                      bool allow_retry = false,
                      uint32_t tracking_id = 0);
  void drop_pending_polls_();
  void process_tx_queue_();
};

}  // namespace arc_bridge
}  // namespace esphome

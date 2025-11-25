#include "arc_bridge.h"
#include "esphome/core/log.h"

namespace esphome {
namespace arc_bridge {

static const char *const TAG = "arc_bridge";

static const uint32_t STARTUP_GUARD_MS = 3000;   // block polling after boot
static const uint32_t MOVE_INHIBIT_MS  = 90000;  // inhibit polling after any move (90s)
static const uint32_t POLL_RETRY_MS    = 600;    // re-query if no reply
static const uint32_t POLL_INTERVAL_MS = 15000;  // default poll interval

void ARCBridgeComponent::setup() {
  // Flush UART noise
  while (this->available()) this->read();

  this->boot_millis_ = millis();
  this->last_poll_millis_ = 0;
  this->startup_guard_cleared_ = false;

  ESP_LOGI(TAG, "ARCBridge setup: poll interval %u ms", POLL_INTERVAL_MS);
}

void ARCBridgeComponent::register_blind(ARCBlind *blind) {
  this->blinds_.push_back(blind);
}

// ----------------------------------------------------------
// Main loop
// ----------------------------------------------------------
void ARCBridgeComponent::loop() {

  uint32_t now = millis();

  // Startup guard
  if (!this->startup_guard_cleared_ &&
      now - this->boot_millis_ >= STARTUP_GUARD_MS) {
    this->startup_guard_cleared_ = true;
    ESP_LOGI(TAG, "Startup guard cleared");
  }

  // Move inhibit period
  if (this->move_inhibit_active_) {
    if (now - this->last_move_millis_ >= MOVE_INHIBIT_MS) {
      this->move_inhibit_active_ = false;
      ESP_LOGI(TAG, "Move inhibit period cleared");
    }
  }

  // Handle inbound UART bytes
  this->process_incoming_bytes();

  // Poll positions
  if (this->startup_guard_cleared_ &&
      !this->move_inhibit_active_ &&
      (now - this->last_poll_millis_) >= POLL_INTERVAL_MS) {
    this->poll_all_positions();
    this->last_poll_millis_ = now;
  }

  // Retry logic
  this->retry_unanswered_positions();
}

// ----------------------------------------------------------
// Send a queued command to UART
// ----------------------------------------------------------
void ARCBridgeComponent::queue_command(const std::string &cmd) {
  ESP_LOGD(TAG, "Queue TX: %s", cmd.c_str());
  this->write_str(cmd.c_str());
}

// ----------------------------------------------------------
// Send per-blind direct position query !IDr?;
// ----------------------------------------------------------
void ARCBridgeComponent::poll_all_positions() {
  ESP_LOGD(TAG, "Polling all blind positions (direct ID poll)");

  for (auto *blind : this->blinds_) {
    this->send_position_query(blind->blind_id_);
    blind->last_query_time_ = millis();
    blind->awaiting_reply_ = true;
    delay(50);  // spacing to avoid collisions
  }
}

void ARCBridgeComponent::send_position_query(const std::string &id) {
  std::string cmd = "!" + id + "r?;";
  this->queue_command(cmd);
}

// ----------------------------------------------------------
// Retry missing replies
// ----------------------------------------------------------
void ARCBridgeComponent::retry_unanswered_positions() {
  uint32_t now = millis();

  for (auto *blind : this->blinds_) {
    if (blind->awaiting_reply_ &&
        (now - blind->last_query_time_) > POLL_RETRY_MS) {
      ESP_LOGW(TAG, "Retry position query for %s", blind->blind_id_.c_str());
      this->send_position_query(blind->blind_id_);
      blind->last_query_time_ = now;
    }
  }
}

// ----------------------------------------------------------
// Movement command handler — enter inhibit mode
// ----------------------------------------------------------
void ARCBridgeComponent::blind_moved() {
  this->move_inhibit_active_ = true;
  this->last_move_millis_ = millis();
  ESP_LOGI(TAG, "Move command sent → polling inhibited for 90 sec");
}

// ----------------------------------------------------------
// Incoming UART parsing (trimmed for clarity)
// ----------------------------------------------------------
void ARCBridgeComponent::process_incoming_bytes() {
  while (this->available()) {
    uint8_t c = this->read();
    this->rx_buffer_.push_back(c);

    if (c == ';') {  // end of packet
      this->handle_packet(this->rx_buffer_);
      this->rx_buffer_.clear();
    }
  }
}

void ARCBridgeComponent::handle_packet(const std::string &pkt) {
  ESP_LOGD(TAG, "RX: %s", pkt.c_str());

  // Expected form: :IDPCT; or similar
  if (pkt.length() < 5) return;

  // Extract blind ID
  std::string id = pkt.substr(1, 3);

  for (auto *blind : this->blinds_) {
    if (blind->blind_id_ == id) {

      // Mark reply received
      blind->awaiting_reply_ = false;

      // Extract % position (simple parse)
      int pct = atoi(pkt.substr(4).c_str());
      blind->set_position_from_motor(pct);

      break;
    }
  }
}

}  // namespace arc_bridge
}  // namespace esphome

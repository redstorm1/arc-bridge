#pragma once

#include "protocol.h"

#include <cstdint>
#include <string>

namespace esphome {
namespace arc_bridge {

struct PairingSession {
  bool active{false};
  uint32_t started_ms{0};
};

enum class PairingOutcomeType {
  NONE,
  SUCCESS,
  ERROR,
  TIMEOUT,
  GENERIC_ACK,
};

struct PairingOutcome {
  PairingOutcomeType type{PairingOutcomeType::NONE};
  std::string message;
  std::string paired_id;
};

std::string build_pair_command_frame();
void start_pairing_session(PairingSession &session, uint32_t now_ms);
PairingOutcome handle_pairing_frame(PairingSession &session, const ParsedFrame &parsed);
PairingOutcome check_pairing_timeout(PairingSession &session, uint32_t now_ms, uint32_t timeout_ms);
std::string describe_error_code(const std::string &code);

}  // namespace arc_bridge
}  // namespace esphome

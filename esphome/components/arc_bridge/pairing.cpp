#include "pairing.h"

namespace esphome {
namespace arc_bridge {

namespace {

void clear_pairing_session_(PairingSession &session) {
  session.active = false;
  session.started_ms = 0;
}

}  // namespace

std::string build_pair_command_frame() {
  return "!000&;";
}

void start_pairing_session(PairingSession &session, uint32_t now_ms) {
  session.active = true;
  session.started_ms = now_ms;
}

std::string describe_error_code(const std::string &code) {
  if (code == "bz") {
    return "Hub busy";
  }
  if (code == "df") {
    return "Hub motor limit exceeded";
  }
  if (code == "np") {
    return "Invalid motor address";
  }
  if (code == "nc") {
    return "Limits not set";
  }
  if (code == "mh") {
    return "Master Hall sensor abnormal";
  }
  if (code == "sh") {
    return "Slave Hall sensor abnormal";
  }
  if (code == "or") {
    return "Obstacle during upper movement";
  }
  if (code == "cr") {
    return "Obstacle during down movement";
  }
  if (code == "pl") {
    return "Low voltage alarm";
  }
  if (code == "ph") {
    return "High voltage alarm";
  }
  if (code == "nl") {
    return "No response from motor";
  }
  if (code == "ec") {
    return "Undefined error";
  }
  return "Protocol error " + code;
}

PairingOutcome handle_pairing_frame(PairingSession &session, const ParsedFrame &parsed) {
  if (parsed.address_ack) {
    if (!session.active) {
      return {PairingOutcomeType::GENERIC_ACK, "", parsed.id};
    }

    const std::string paired_id = parsed.id;
    clear_pairing_session_(session);
    return {PairingOutcomeType::SUCCESS, "Paired", paired_id};
  }

  if (session.active && static_cast<bool>(parsed.error_code)) {
    const std::string message = "Error: " + describe_error_code(*parsed.error_code);
    clear_pairing_session_(session);
    return {PairingOutcomeType::ERROR, message, ""};
  }

  return {};
}

PairingOutcome check_pairing_timeout(PairingSession &session, uint32_t now_ms, uint32_t timeout_ms) {
  if (!session.active) {
    return {};
  }

  if (now_ms - session.started_ms < timeout_ms) {
    return {};
  }

  clear_pairing_session_(session);
  return {PairingOutcomeType::TIMEOUT, "Timed Out", ""};
}

}  // namespace arc_bridge
}  // namespace esphome

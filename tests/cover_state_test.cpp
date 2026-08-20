#include "esphome/components/arc_bridge/arc_cover.h"
#include "esphome/components/arc_bridge/arc_bridge.h"
#include "esphome/components/arc_bridge_group/arc_bridge_group_cover.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using esphome::arc_bridge::ARCCover;
using esphome::arc_bridge_group::ARCBridgeGroupCover;

namespace esphome {
namespace arc_bridge {
void ARCBridgeComponent::send_open(const std::string &) {}
void ARCBridgeComponent::send_close(const std::string &) {}
void ARCBridgeComponent::send_stop(const std::string &) {}
void ARCBridgeComponent::send_move(const std::string &, uint8_t) {}
}  // namespace arc_bridge
}  // namespace esphome

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
  }
}

void test_unavailable_cover_never_publishes_nan() {
  ARCCover cover;
  int updates = 0;
  float published_position = 0.0f;
  cover.add_on_state_callback([&]() {
    updates++;
    published_position = cover.position;
  });

  cover.set_available(false);
  require(updates == 0, "a cover without a known position should not publish availability");
  cover.set_available(true);
  require(updates == 0, "availability alone should not invent a cover position");
  require(!cover.has_state(), "a cover should wait for its first real position");

  cover.publish_raw_position(25);
  require(updates == 1, "a valid position should publish once");
  cover.set_available(false);
  require(updates == 2, "an unavailable cover with known state should publish idle once");
  require(std::isfinite(published_position), "an unavailable cover must publish a finite position");
}

void test_group_without_position_never_publishes_nan() {
  ARCCover member;
  ARCBridgeGroupCover group;
  int updates = 0;
  float published_position = 0.0f;
  group.add_member(&member);
  group.add_on_state_callback([&]() {
    updates++;
    published_position = group.position;
  });

  group.setup();
  require(updates == 0, "a group without a known member position should not publish");

  member.publish_raw_position(25);
  require(updates == 1, "a group should publish its first known position");
  member.set_available(false);
  require(updates == 2, "a group should publish idle when its known members become unavailable");
  require(std::isfinite(published_position), "an unavailable group must publish a finite position");
}

}  // namespace

int main() {
  test_unavailable_cover_never_publishes_nan();
  test_group_without_position_never_publishes_nan();
  std::cout << "cover state tests passed" << std::endl;
  return 0;
}

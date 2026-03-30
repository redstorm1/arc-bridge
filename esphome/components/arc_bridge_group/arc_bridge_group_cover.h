#pragma once

#include "esphome/core/component.h"
#include "esphome/components/arc_bridge/arc_cover.h"
#include "esphome/components/cover/cover.h"

#include <vector>

namespace esphome {
namespace arc_bridge_group {

class ARCBridgeGroupCover : public cover::Cover, public Component {
 public:
  void add_member(arc_bridge::ARCCover *member) { this->members_.push_back(member); }

  void setup() override;
  void dump_config() override;
  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
  void recompute_state_();

  std::vector<arc_bridge::ARCCover *> members_;
};

}  // namespace arc_bridge_group
}  // namespace esphome

#pragma once

#include "esphome/components/cover/cover.h"
#include <string>

namespace esphome {
namespace arc_bridge {

class ARCBridgeComponent;

class ARCCover : public cover::Cover {
 public:
  void set_bridge(ARCBridgeComponent *bridge) { bridge_ = bridge; }
  void set_id(const std::string &id) { id_ = id; }
  const std::string &get_id() const { return id_; }

  cover::CoverTraits get_traits() override;
  void control(const cover::CoverCall &call) override;

  void publish_raw(int arc_pos);

 protected:
  ARCBridgeComponent *bridge_{nullptr};
  std::string id_;
};

}  // namespace arc_bridge
}  // namespace esphome

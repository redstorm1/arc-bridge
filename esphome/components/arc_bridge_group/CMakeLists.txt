esphome_component(
  NAME arc_bridge_group
  SRCS
    "arc_bridge_group_cover.cpp"
  HDRS
    "arc_bridge_group_cover.h"
  REQUIRES
    "arc_bridge"
    "cover"
)

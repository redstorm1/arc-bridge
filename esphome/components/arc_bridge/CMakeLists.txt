esphome_component(
  NAME arc_bridge
  SRCS
    "arc_bridge.cpp"
    "arc_cover.cpp"
  HDRS
    "arc_bridge.h"
    "arc_cover.h"
  REQUIRES
    "uart"
    "cover"
    "sensor"
    "text_sensor"
)

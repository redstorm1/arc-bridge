esphome_component(
  NAME arc_bridge
  SRCS "arc_bridge.cpp"
  HDRS "arc_bridge.h"
  REQUIRES "uart;cover;sensor;text_sensor"
)
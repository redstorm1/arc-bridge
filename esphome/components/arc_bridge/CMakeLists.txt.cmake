esphome_component(
  NAME arc_bridge
  SRCS "arc_bridge.cpp" "arc_cover.cpp" "battery.cpp" "delivery.cpp" "pairing.cpp" "protocol.cpp" "tx_queue.cpp"
  HDRS "arc_bridge.h" "arc_cover.h" "battery.h" "delivery.h" "pairing.h" "protocol.h" "tx_queue.h"
  REQUIRES "uart;cover;sensor;text_sensor"
)

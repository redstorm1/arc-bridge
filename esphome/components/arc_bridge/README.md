# ARC Bridge External Component for ESPHome

This external component allows direct integration of **Dooya ARC / Pulse 2** RF blinds with ESPHome running on ESP32 boards.

---

## Features

- Full UART protocol parsing (`!IDr###;`, `!IDm###;`, `!IDEnp;`, `!IDEnl;`)
- Individual blind control (`open`, `close`, `stop`, `set_position`)
- Automatic RF link quality sensors
- Status tracking:
  - **online**
  - **offline** (no reply for >60s)
  - **not paired** (`!IDEnp;`)
- Works with LAN8720 Ethernet, MQTT, or local Home Assistant API

---

## Example YAML

```yaml

esphome:
  name: pulse2hubdev
  friendly_name: pulse2hubdev
  on_boot:
    priority: -200
    then:
      - logger.log: "Waiting for Ethernet link ..."
      - wait_until:
          lambda: 'return id(eth0).is_connected();'
      - logger.log: "Ethernet up"
      - switch.turn_on: green


esp32:
  board: esp32dev
  framework:
    type: arduino
    version: recommended

logger:
  level: DEBUG
  baud_rate: 0       # disable serial logging, free UART0 pins

ethernet:
  id: eth0
  type: LAN8720
  mdc_pin: GPIO16
  mdio_pin: GPIO23
  clk:
    pin: GPIO0
    mode: CLK_EXT_IN
  phy_addr: 1
  power_pin: GPIO2

api:
  encryption:
    key: "W2NcdU3qMfp7UQbgS832RIP+DWCqQbjgZeVztbov2CY="

ota:
  - platform: esphome
    password: "cb2a0f65d905cf5e8509ac73ab55a7ee"

web_server:
  port: 80

# ──────────────────────────────────────────────
# I2C & PCA9554 (LEDs)
# ──────────────────────────────────────────────
i2c:
  id: i2c_bus
  sda: GPIO14
  scl: GPIO4
  frequency: 100kHz
  scan: false

pca9554:
  - id: pca9554a_device
    i2c_id: i2c_bus
    address: 0x41

switch:
  - platform: gpio
    name: "LED Red"
    id: red
    pin:
      pca9554: pca9554a_device
      number: 0
      mode: { output: true }
      inverted: true

  - platform: gpio
    name: "LED Blue"
    id: blue
    pin:
      pca9554: pca9554a_device
      number: 1
      mode: { output: true }
      inverted: true

  - platform: gpio
    name: "LED Green"
    id: green
    pin:
      pca9554: pca9554a_device
      number: 2
      mode: { output: true }
      inverted: true

  # - platform: gpio
  #   name: "Switch4"
  #   id: stm32_en
  #   pin:
  #     pca9554: pca9554a_device
  #     number: 4
  #     mode:
  #       output: true
  #     inverted: true

button:
  - platform: template
    name: "ARC Bridge Pairing"
    icon: "mdi:link-plus"
    on_press:
      - lambda: |-
          id(arc)->send_pair_command();
  - platform: template
    name: "ARC Bridge Version"
    icon: "mdi:link-plus"
    on_press:
      - lambda: |-
          id(arc)->send_simple("000", 'v', "?");
  - platform: template
    name: "ARC Bridge Global Position"
    icon: "mdi:link-plus"
    on_press:
      - lambda: |-
          id(arc)->send_simple("000", 'r', "?");

#──────────────────────────────────────────────
#External Buttons
#──────────────────────────────────────────────
binary_sensor:
  - platform: gpio
    name: "Button GPIO36 Pair"
    pin:
      number: GPIO36
      mode:
        input: true
      inverted: true
    on_press:
      - lambda: |-
          id(arc)->send_pair_command();

  - platform: gpio
    name: "External Button (GPIO39)"
    pin:
      number: GPIO39
      mode:
        input: true
      inverted: true        # flip to true if the logic is active-low

# ──────────────────────────────────────────────
# Inputs
# ──────────────────────────────────────────────

# text:
#   - platform: template
#     id: blind_id
#     name: "Dooya Blind ID"
#     mode: TEXT
#     initial_value: "USZ"
#     optimistic: true
#     min_length: 1
#     max_length: 8
#   - platform: template
#     id: custom_cmd
#     name: "Custom RF Command"
#     optimistic: true
#     mode: text
#     min_length: 0
#     max_length: 64
#     initial_value: ""

external_components:
  - source: github://redstorm1/arc-bridge
    components: [arc_bridge]
    refresh: 1s   # optional while iterating to force refetch

# ──────────────────────────────────────────────
# UART (STM32 link)
# ──────────────────────────────────────────────
uart:
  - id: rf_a
    rx_pin: GPIO13    # STM32 RX ← ESP TX
    tx_pin: GPIO15    # STM32 TX → ESP RX
    baud_rate: 115200
    data_bits: 8
    parity: NONE
    stop_bits: 1
    rx_buffer_size: 4096

arc_bridge:
  id: arc
  uart_id: rf_a


cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    device_class: shade
    name: "Office Blind"
    blind_id: "USZ"
    link_quality: lq_usz
    status: status_usz

  - platform: arc_bridge
    bridge_id: arc
    id: zxe
    device_class: shade
    name: "Guest Blind"
    blind_id: "ZXE"
    link_quality: lq_zxe
    status: status_zxe

  - platform: arc_bridge
    bridge_id: arc
    id: nom
    device_class: curtain
    name: "Living Drape"
    blind_id: "NOM"
    link_quality: lq_nom
    status: status_nom

  - platform: arc_bridge
    bridge_id: arc
    id: ovj
    device_class: curtain
    name: "Guest Drape"
    blind_id: "OVJ"
    link_quality: lq_ovj
    status: status_ovj

  - platform: arc_bridge
    bridge_id: arc
    id: txy
    device_class: shade
    name: "Living Window Blind"
    blind_id: "TXY"
    link_quality: lq_txy
    status: status_txy

  - platform: arc_bridge
    bridge_id: arc
    id: mlt
    device_class: shade
    name: "Living Door Blind"
    blind_id: "MLT"
    link_quality: lq_mlt
    status: status_mlt

sensor:
  - platform: template
    id: lq_usz
    name: "Office Blind Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_zxe
    name: "Guest Blind Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_nom
    name: "Living Drape Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_ovj
    name: "Guest Drape Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_txy
    name: "Living Window Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_mlt
    name: "Living Door Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"

text_sensor:
  - platform: template
    id: status_usz
    name: "Office Blind Status"
  - platform: template
    id: status_zxe
    name: "Guest Blind Status"
  - platform: template
    id: status_nom
    name: "Living Drape Status"
  - platform: template
    id: status_ovj
    name: "Guest Drape Status"
  - platform: template
    id: status_txy
    name: "Living Window Status"
  - platform: template
    id: status_mlt
    name: "Living Door Status"
    ```
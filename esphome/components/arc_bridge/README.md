# ARC Bridge External Component for ESPHome

This external component allows direct integration of Dooya ARC / Pulse 2 RF blinds with ESPHome running on ESP32 boards.

It talks to the Pulse 2 board firmware over UART using 3-character blind IDs. It is **not a raw Pulse 1 RS485 implementation**, so hub-addressed frames like `!XXXDYYY...;` remain out of scope for this component.

---

## Features

- Full UART protocol parsing (`!IDr###;`, `!IDEnp;`, `!IDEnl;`, `!IDpVc###;`)
- Individual blind control (`open`, `close`, `stop`, `set_position`)
- Group covers using `arc_bridge_group`
- Automatic RF link quality and status sensors
- Optional voltage, battery level, speed, version, and limits sensors
- Random-assignment pairing with bridge-level feedback sensors
- Safe manual actions for pairing, refresh, favorite, and jog control
- Works with `esp-idf` and Arduino ESPHome frameworks

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
    type: esp-idf

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

ota:
  - platform: esphome

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

button:
  - platform: template
    name: "ARC Bridge Pairing"
    icon: "mdi:link-plus"
    entity_category: config
    on_press:
      - lambda: |-
          id(arc)->send_pair_command();

  - platform: template
    name: "ARC Bridge Refresh"
    icon: "mdi:refresh"
    entity_category: diagnostic
    on_press:
      - lambda: |-
          id(arc)->send_query_all();

  - platform: template
    name: "Office Blind Favorite"
    icon: "mdi:star"
    on_press:
      - lambda: |-
          id(arc)->send_favorite("USZ");

  - platform: template
    name: "Office Blind Jog Open"
    icon: "mdi:arrow-up-bold"
    on_press:
      - lambda: |-
          id(arc)->send_jog_open("USZ");

  - platform: template
    name: "Office Blind Jog Close"
    icon: "mdi:arrow-down-bold"
    on_press:
      - lambda: |-
          id(arc)->send_jog_close("USZ");

#──────────────────────────────────────────────
#External Buttons
#──────────────────────────────────────────────
binary_sensor:
  - platform: gpio
    name: "Button Pair"
    pin:
      number: GPIO36
      mode:
        input: true
      inverted: true
    on_press:
      - lambda: |-
          id(arc)->send_pair_command();

  - platform: gpio
    name: "Button Reboot"
    pin:
      number: GPIO39
      mode:
        input: true
      inverted: true
    on_press:
      then:
        - lambda: |-
            ESP_LOGI("reboot", "Manual reboot triggered (GPIO39)");
            App.safe_reboot();

external_components:
  - source: github://redstorm1/arc-bridge
    components: [arc_bridge, arc_bridge_group]
    refresh: 1s

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
  auto_poll: true
  auto_poll_interval: 10s
  motion_tx_gap: 200ms
  command_retries: 1
  command_retry_timeout: 1500ms
  pairing_status: pairing_status
  last_paired_id: last_paired_id

cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    device_class: shade
    name: "Office Blind"
    blind_id: "USZ"
    link_quality: lq_usz
    status: status_usz
    version: version_usz
    speed: speed_usz
    limits: limits_usz
    power: power_usz
    battery_level: battery_usz

  - platform: arc_bridge
    bridge_id: arc
    id: workshop_drape
    device_class: curtain
    name: "Workshop Drape"
    blind_id: "WRK"
    link_quality: lq_wrk
    status: status_wrk

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

  - platform: arc_bridge_group
    id: living_room
    name: "Living Room"
    members: [nom, txy, mlt]

sensor:
  - platform: template
    id: lq_usz
    name: "Office Blind Link Quality"
    entity_category: diagnostic
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_zxe
    name: "Guest Blind Link Quality"
    entity_category: diagnostic
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_nom
    name: "Living Drape Link Quality"
    entity_category: diagnostic
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_ovj
    name: "Guest Drape Link Quality"
    entity_category: diagnostic
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_txy
    name: "Living Window Link Quality"
    entity_category: diagnostic
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_mlt
    name: "Living Door Link Quality"
    entity_category: diagnostic
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: lq_wrk
    name: "Workshop Drape Link Quality"
    entity_category: diagnostic
    unit_of_measurement: "dBm"
    icon: "mdi:signal"
  - platform: template
    id: power_usz
    name: "Office Blind Voltage"
    entity_category: diagnostic
    unit_of_measurement: "V"
    accuracy_decimals: 2
    icon: "mdi:battery"
  - platform: template
    id: battery_usz
    name: "Office Blind Battery"
    unit_of_measurement: "%"
    accuracy_decimals: 0
    device_class: battery
  - platform: template
    id: speed_usz
    name: "Office Blind Speed"
    entity_category: diagnostic
    unit_of_measurement: "rpm"
    icon: "mdi:speedometer"

text_sensor:
  - platform: template
    id: pairing_status
    name: "ARC Pairing Status"
    entity_category: diagnostic
  - platform: template
    id: last_paired_id
    name: "ARC Last Paired ID"
    entity_category: diagnostic
  - platform: template
    id: status_usz
    name: "Office Blind Status"
    entity_category: diagnostic
  - platform: template
    id: status_wrk
    name: "Workshop Drape Status"
    entity_category: diagnostic
  - platform: template
    id: status_zxe
    name: "Guest Blind Status"
    entity_category: diagnostic
  - platform: template
    id: status_nom
    name: "Living Drape Status"
    entity_category: diagnostic
  - platform: template
    id: status_ovj
    name: "Guest Drape Status"
    entity_category: diagnostic
  - platform: template
    id: status_txy
    name: "Living Window Status"
    entity_category: diagnostic
  - platform: template
    id: status_mlt
    name: "Living Door Status"
    entity_category: diagnostic
  - platform: template
    id: version_usz
    name: "Office Blind Version"
    entity_category: diagnostic
  - platform: template
    id: limits_usz
    name: "Office Blind Limits"
    entity_category: config
```

Arduino is still supported if you want to use `framework.type: arduino`.

`power:` is shown above for backwards compatibility. New configs can also use `voltage:`.

Use `device_class: shade` for roller / roman / zebra / cellular-style blinds and `device_class: curtain` for drapery / curtain motors.

## Notes

- `auto_poll_interval` is per blind. The bridge rotates through known blinds instead of polling all blinds at once.
- `motion_tx_gap` applies to motion commands such as open, close, stop, move, favorite, and jog.
- `command_retries` and `command_retry_timeout` control the verification-and-retry path for safe motion commands.
- `members:` in `arc_bridge_group` takes existing `arc_bridge` cover IDs.
- Grouped motion is still serialized one blind at a time.

## Optional Sensor Values

The optional sensors above are updated from ARC replies:

- `pairing_status`: `Pairing`, `Paired`, `Timed Out`, or `Error: ...`
- `last_paired_id`: the blind ID returned by a successful pairing acknowledgement
- `status`: `Online`, `Offline`, `Not Paired`, `No Position`
- `version`: decoded motor type/version such as `AC v2.1`
- `limits`: `Unset`, `Upper/Lower Set`, `Upper/Lower/Preferred Set`
- `power` / `voltage`: `0.00 V` means AC or mains-powered
- `battery_level`: derived 3S Li-ion estimate from `pVc`

There are no built-in discovery, refresh, or pairing ESPHome services in this repo. Use the bridge methods above instead.

Any older YAML lambda calling `send_pair_command_with_id(...)` should be updated to `send_pair_command()`. This hardware only pairs by assigning a random ID to the newly paired device.

## Protocol Notes

- Final-position replies such as `!USZr100b180;`
- In-motion replies such as `!USZ<09b00;`
- Pairing/admin acknowledgements such as `!QJ0A;`
- Generic protocol errors such as `!QJ0Edf;`
- Lost-link `Enl` and not-paired `Enp`
- Voltage `pVc`, speed `pSc`, limits `pP`, and version `v?`

During an active pairing session, `!XXXA;` is interpreted as pairing success. Outside pairing, it is treated as a generic admin acknowledgement so unsolicited `A` frames do not produce false pair-success events.

The component does not expose destructive admin commands such as address rewrites, delete/unpair operations, or factory reset as first-class YAML features.

Tilt-capable devices are currently treated as basic position covers only. Version text that decodes to non-cover types such as `Socket` or `Lighting` is informational and does not auto-select entity behavior or create first-class non-cover entities.

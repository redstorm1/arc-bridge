# ESPHome ARC Bridge Component

This component is for use with the Pulse 2 Hub after installing ESPHome onto the hardware:
https://www.geektech.co.nz/esphome-pulse-2-hub

This ESPHome component implements the Rollease Acmeda ARC ASCII serial protocol over an ESP32 UART interface.
It allows direct control of ARC blinds without the original Pulse 2 Hub — supporting full cover control, automatic discovery, and live feedback (RSSI, status, and position).

## Features

- Full cover entity control (`open`, `close`, `stop`, `move to %`)
- Group covers using `arc_bridge_group`
- Round-robin polling for position, link quality, and static blind data
- Optional link quality, status, voltage, battery level, speed, version, and limits sensors
- Pairing, refresh, favorite, and jog actions from ESPHome buttons
- Works with ESPHome on both `esp-idf` and Arduino

## Installation

```yaml
esp32:
  board: esp32dev
  framework:
    type: esp-idf

external_components:
  - source: github://redstorm1/arc-bridge
    components: [arc_bridge, arc_bridge_group]
    refresh: 1s

# ──────────────────────────────────────────────
# UART (STM32 link)
# ──────────────────────────────────────────────
uart:
  - id: rf_a
    rx_pin: GPIO13    # ESP RX  ← STM32 TX
    tx_pin: GPIO15    # ESP TX  → STM32 RX
    baud_rate: 115200
    data_bits: 8
    parity: NONE
    stop_bits: 1
    rx_buffer_size: 4096

arc_bridge:
  id: arc
  uart_id: rf_a
  auto_poll: true        # set false to disable queries entirely
  auto_poll_interval: 10s
  motion_tx_gap: 200ms
  command_retries: 1
  command_retry_timeout: 1500ms
```

Arduino is still supported if you prefer `framework.type: arduino`.

## Auto-Poll (Recommended)

The bridge rotates through known blinds and queries them one at a time for position and RF status.

| Setting | Description | Default |
|--------:|-------------|---------|
| `auto_poll` | Enables background polling | `true` |
| `auto_poll_interval` | Time between each blind query | `10s` |
| `motion_tx_gap` | Internal spacing for motion commands | `200ms` |
| `command_retries` | Retries safe motion commands after a missed reply | `1` |
| `command_retry_timeout` | Wait time before verification and retry | `1500ms` |

Setting `auto_poll_interval: 0s` disables polling completely.

## Cover Entities

```yaml
cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    device_class: shade
    name: "Office Blind"
    blind_id: "USZ"
    link_quality: lq_usz
    status: status_usz
    voltage: voltage_usz
    battery_level: battery_usz
```

Each cover supports open, close, stop, and set position.

New configs should use `voltage:`. The legacy `power:` key is still accepted for backwards compatibility.

If you want to control several blinds as one cover:

```yaml
cover:
  - platform: arc_bridge_group
    id: living_room
    name: "Living Room"
    members: [usz, khn, hw4, j8u]
```

`members:` takes existing `arc_bridge` cover IDs. Grouped moves are still sent one blind at a time, just with the faster motion gap.

## Optional Sensors

```yaml
sensor:
  - platform: template
    id: lq_usz
    name: "Office Blind Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"

  - platform: template
    id: voltage_usz
    name: "Office Blind Voltage"
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
    unit_of_measurement: "rpm"
    icon: "mdi:speedometer"

text_sensor:
  - platform: template
    id: status_usz
    name: "Office Blind Status"

  - platform: template
    id: version_usz
    name: "Office Blind Version"

  - platform: template
    id: limits_usz
    name: "Office Blind Limits"
```

These are updated from ARC messages:

- `status`: `Online`, `Offline`, `Not Paired`, `No Position`
- `version`: decoded motor type/version such as `AC v2.1`
- `limits`: `Unset`, `Upper/Lower Set`, `Upper/Lower/Preferred Set`
- `voltage` / `power`: `0.00 V` indicates an AC or mains-powered motor
- `battery_level`: derived from `pVc` using a fixed 3S Li-ion curve

## Manual Actions

You can trigger pairing and other safe bridge actions directly from ESPHome or Home Assistant:

```yaml
button:
  - platform: template
    name: "ARC Pairing"
    on_press:
      - lambda: |-
          id(arc)->send_pair_command();

  - platform: template
    name: "ARC Query All"
    on_press:
      - lambda: |-
          id(arc)->send_query_all();

  - platform: template
    name: "Office Blind Favorite"
    on_press:
      - lambda: |-
          id(arc)->send_favorite("USZ");

  - platform: template
    name: "Office Blind Jog Open"
    on_press:
      - lambda: |-
          id(arc)->send_jog_open("USZ");

  - platform: template
    name: "Office Blind Jog Close"
    on_press:
      - lambda: |-
          id(arc)->send_jog_close("USZ");
```

There are no built-in discovery or pairing ESPHome services in this repo. Use the bridge methods above instead.

## Protocol Details

Standard frame format: `!<id><command><data>;`

Examples:

- `!USZr100b180;` -> Blind `USZ`, position `100`, link quality present
- `!USZEnl;` -> Lost link
- `!USZEnp;` -> Not paired

Position mapping: ARC `0 = open` -> HA `1.0`, ARC `100 = closed` -> HA `0.0`

## Known Limitations

- No encrypted ARC+ protocol support
- No raw Pulse 1 hub-address mode such as `!XXXDYYY...;`
- Static values like version and limits are refreshed when first discovered and when `send_query_all()` is used

## License

MIT License

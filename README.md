# ESPHome ARC Bridge Component

This component is for use with the Pulse 2 Hub after installing ESPHome onto the hardware:
https://www.geektech.co.nz/esphome-pulse-2-hub

It talks to the Pulse 2 board firmware over the onboard UART bridge using 3-character blind IDs. It is **not a raw Pulse 1 RS485 implementation**, so this repo intentionally stays Pulse 2/UART-first even when it borrows safe query ideas from the Pulse 1 protocol reference.

## Features

- Cover control for `open`, `close`, `stop`, and `set_position`
- First-class group covers built from existing `arc_bridge` covers
- Round-robin auto-polling for per-blind position updates
- Optional per-blind sensors for link quality, status, voltage, derived battery level, speed, version, and limits state
- Safe manual bridge actions for pairing, refresh, favorite position, and jog open/close
- ESPHome support on both `esp-idf` and Arduino

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

uart:
  - id: rf_a
    rx_pin: GPIO13
    tx_pin: GPIO15
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
```

`auto_poll_interval` is the time between each blind query, not a full sweep. The bridge polls one blind at a time in round-robin order so larger installs do not burst the UART bus every cycle.

## Cover Configuration

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
    version: version_usz
    speed: speed_usz
    limits: limits_usz
    voltage: voltage_usz
    battery_level: battery_usz
```

Each cover supports open, close, stop, and set position. New configs should use `voltage:`. The legacy `power:` key is still accepted for backwards compatibility.

## Group Covers

```yaml
cover:
  - platform: arc_bridge_group
    id: living_room
    name: "Living Room"
    members: [usz, khn, hw4, j8u]
```

`members:` references existing `arc_bridge` cover IDs, so the group behaves like a normal ESPHome cover without `lambda` fan-out.

Group motion is still serialized one blind at a time, but motion commands now use a shorter internal `200 ms` send gap while poll and static query traffic stay on the slower conservative pacing.

```yaml
button:
  - platform: template
    name: "Living Room to 50%"
    on_press:
      - cover.control:
          id: living_room
          position: 50%

  - platform: template
    name: "Open Living Room"
    on_press:
      - cover.open: living_room
```

## Optional Sensors

```yaml
sensor:
  - platform: template
    id: lq_usz
    name: "Office Blind Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"

  - platform: template
    id: speed_usz
    name: "Office Blind Speed"
    unit_of_measurement: "rpm"
    icon: "mdi:speedometer"

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

Status values currently include `Online`, `Offline`, `Not Paired`, and `No Position`.

`version` publishes the decoded motor type and version, for example `AC v2.1`.

`limits` publishes human-readable values:

- `Unset`
- `Upper/Lower Set`
- `Upper/Lower/Preferred Set`

A voltage reading of `0.00 V` indicates an AC or mains-powered motor.

`battery_level` is a derived estimate from `pVc` using a fixed 3S Li-ion curve. It is a convenience estimate rather than a precise fuel gauge, and AC motors leave it unavailable.

## Manual Actions

Use template buttons or lambdas for safe manual actions instead of relying on undocumented services.

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

## Protocol Notes

- The component understands position replies such as `!USZr100b180;` and in-motion replies such as `!USZ<09b00;`
- Voltage uses `pVc`, speed uses `pSc`, limits use `pP`, and version uses `v?`
- Lost-link `Enl` and not-paired `Enp` states are handled distinctly
- Motion commands use a shorter internal send gap than poll/static queries
- The code does not expose destructive Pulse 1-style admin commands such as address rewrites, resets, or factory defaults as first-class YAML features

## Limitations

- No encrypted ARC+ protocol support
- No raw Pulse 1 hub-address mode such as `!XXXDYYY...;`
- Static values like version and limits are only refreshed automatically when first discovered or when `send_query_all()` is called

## License

MIT License

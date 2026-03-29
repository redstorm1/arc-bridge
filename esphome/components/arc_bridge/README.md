# ARC Bridge External Component for ESPHome

This external component integrates Dooya ARC / Pulse 2 blinds with ESPHome running on ESP32 boards.

It talks to the Pulse 2 board firmware over UART using 3-character blind IDs and blind-level ARC frames such as `!USZr?;`. It is **not a raw Pulse 1 RS485 implementation**, so hub-addressed frames like `!XXXDYYY...;` remain out of scope for this component.

## Highlights

- Per-blind cover control
- Round-robin auto-polling
- Optional link-quality, status, voltage, speed, version, and limits sensors
- Safe manual actions for pairing, refresh, favorite position, and jog control
- Support for both `esp-idf` and Arduino ESPHome frameworks

## Minimal Setup

```yaml
esp32:
  board: esp32dev
  framework:
    type: esp-idf

external_components:
  - source: github://redstorm1/arc-bridge
    components: [arc_bridge]
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

`auto_poll_interval` is per blind. Each interval queues the next valid blind in round-robin order instead of polling every blind at once.

## Cover Example

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
    power: power_usz
```

`power:` is still supported for backwards compatibility. New configs can also use `voltage:`.

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
    id: power_usz
    name: "Office Blind Voltage"
    unit_of_measurement: "V"
    accuracy_decimals: 2
    icon: "mdi:battery"

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

Runtime values:

- `status`: `Online`, `Offline`, `Not Paired`, `No Position`
- `version`: decoded motor type/version such as `AC v2.1`
- `limits`: `Unset`, `Upper/Lower Set`, `Upper/Lower/Preferred Set`
- `power` / `voltage`: `0.00 V` means AC or mains-powered

## Manual Actions

Expose manual actions through template buttons or lambdas:

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

There are no built-in discovery, refresh, or pairing ESPHome services in this repo. Use the bridge methods above instead.

## Protocol Coverage

The component currently handles:

- Final-position replies like `!USZr100b180;`
- In-motion position replies like `!USZ<09b00;`
- Lost-link `Enl` and not-paired `Enp`
- Voltage `pVc`
- Speed `pSc`
- Limits `pP`
- Version `v?`

It does not expose destructive admin commands such as hub resets, address rewrites, delete/unpair operations, or factory reset as first-class YAML features.

## Framework Notes

This component remains compatible with ESPHome on the Arduino framework. Existing Arduino-based nodes can keep the same `arc_bridge`, `bridge_id`, and `uart_id` configuration.

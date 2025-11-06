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
external_components:
  - source: github://redstorm1/arc-bridge
    components: [arc_bridge]

uart:
  id: rf_a
  tx_pin: GPIO15
  rx_pin: GPIO13
  baud_rate: 115200

arc_bridge:
  uart_id: rf_a
  blinds:
    - blind_id: USZ
      name: Office Blind
    - blind_id: ZXE
      name: Guest Blind
    - blind_id: OVJ
      name: Guest Drape
    - blind_id: NOM
      name: Living Drape
    - blind_id: TXY
      name: Living Window Blind
    - blind_id: MLT
      name: Living Door Blind

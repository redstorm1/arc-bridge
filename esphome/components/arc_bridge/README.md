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

cover: []
sensor: []
text_sensor: []


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
  id: arc1
  uart_id: rf_a
  blinds:
    - blind_id: USZ
      name: Office Blind
      link_quality:
        name: Office Blind Link Quality
      status:
        name: Office Blind Status
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

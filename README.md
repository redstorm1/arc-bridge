# ESPHome ARC (Automate/Dooya) RS485 Component

**Status:** alpha (discovery + basic cover control/feedback)

This component speaks the ARC ASCII protocol over an ESP32 UART.
It supports:
- Bus sniffing & discovery (periodic broadcast `!000V?;`)
- Cover entities with position + tilt
- Commands: open/close/stop/move to %/tilt to °
- API services to start/stop discovery and query all

> ARC protocol reference: Rollease Acmeda "ARC Serial Protocol via ESP32".

## Install (as external component)

```yaml
external_components:
  - source:
      type: local
      path: components

uart:
  id: rs485_bus
  tx_pin: GPIO15
  rx_pin: GPIO13
  baud_rate: 115200
  parity: NONE
  stop_bits: 1

# RS485 transceiver enable not handled here; if needed use a GPIO to drive DE/RE.

arc:
  discovery_on_boot: true
  broadcast_interval_ms: 5000
  idle_gap_ms: 30

cover:
  - platform: arc
    name: "Lounge Blind"
    address: "USZ"   # 3-char address observed on your fleet
  - platform: arc
    name: "Bedroom Left"
    address: "101"
```

## Services (via ESPHome API)

- `arc_start_discovery`
- `arc_stop_discovery`
- `arc_query_all`

Call from HA Developer Tools → Services.

## Notes

- ARC frames look like `!<hub>D<motor><cmd><data>;` in the spec. When acting as the hub,
  many fleets accept `!<motor><cmd><data>;` (hub omitted). The driver uses the latter by default.
- Position is mapped: ARC 0=open, 100=closed → HA 1.0=open, 0.0=closed.
- Tilt: 0–180° mapped to HA 0.0–1.0.

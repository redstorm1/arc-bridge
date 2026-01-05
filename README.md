# ESPHome ARC Bridge Component

This component is for use with the Pulse 2 Hub after installing ESPHone onto the hardware.
https://www.geektech.co.nz/esphome-pulse-2-hub

## Status: alpha – live control, RSSI, voltage reporting, pairing

This ESPHome component implements the Rollease Acmeda ARC ASCII serial protocol over an ESP32 UART interface.
It allows direct control of ARC blinds without the original Pulse 2 Hub — supporting full cover control, automatic discovery, and live feedback (RSSI, status, and position).

✨ Features

✅ Full cover entity control (open / close / stop / move to %)

🔍 Bus discovery and polling (!000r?; or !000V?;)

📡 RSSI reporting (in dBm and %)

🟢 Status tracking (Online, Offline, Not paired)

🔄 Automatic availability updates (via Enl / Enp)

🔘 Pairing command button (!000&;)

🧩 Template sensor integration for link quality and status per blind

🧠 Designed for the ARC ASCII protocol used by Rollease Acmeda / Automate / Dooya motors

⚡ Battery / DC Motor Support (via pVc query → voltage sensor)

Protocol reference: Rollease Acmeda “ARC Serial Protocol via ESP32”

## ⚙️ Installation (as External Component)
```
external_components:
  - source: github://redstorm1/arc-bridge
    refresh: 1s   # optional while iterating to force refetch

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
  auto_poll_interval: 180s  # accepts standard ESPHome time strings; 0 also disables polling
```

## Auto-Poll (Recommended)

The bridge rotates through known blinds and queries them for position, RSSI, and availability.

| Setting | Description | Default |
|--------:|--------------|---------|
| `auto_poll` | Enables background polling | `true` |
| `auto_poll_interval` | Time between each blind query | `10s` |
| `0s` | Disables polling completely | off |

Auto-poll pauses automatically when a blind is moving to prevent flooding the bus.

Auto-polling now waits until the bridge’s startup guard has elapsed, preventing blinds from moving immediately after a reboot.
## 🪟 Cover Entities

```
cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    device_class: shade
    name: "Office Blind"
    blind_id: "USZ"
    link_quality: lq_usz
    status: status_usz
    power: power_usz 

```

Each cover supports open, close, stop, and set position (0 = open, 100 = closed).

## Optional 📶 Link Quality, Voltage & Status Sensors

Optionally expose link quality and connection state as individual sensors:
```
sensor:
  - platform: template
    id: lq_usz
    name: "Office Blind Link Quality"
    unit_of_measurement: "dBm"
    icon: "mdi:signal"

  - platform: template
    id: power_usz
    name: "Office Blind Voltage"
    unit_of_measurement: "V" # A reading of 0.00V indicates an AC or mains-powered motor.
    accuracy_decimals: 2
    icon: "mdi:battery"

text_sensor:
  - platform: template
    id: status_usz
    name: "Office Blind Status"
```
These are automatically updated from ARC messages:

Frame Type	Example	Action
RSSI Report	!USZr100b180,RA6;	Updates position to 100% and RSSI ≈ −90 dBm (~17%) → Status = Online
Lost Link	!USZEnl;	Clears link quality and sets status = Offline
Not Paired	!USZEnp;	Clears link quality and sets status = Not Paired

RSSI scaling: −100 dBm = 0 % · −40 dBm = 100 %

## 🔘 Pairing Button

You can trigger the blind pairing process directly from ESPHome or Home Assistant:
```
button:

platform: template
name: "ARC Pairing"
icon: "mdi:link-plus"
on_press:

lambda: |-
id(arc)->send_simple("000", '&', "");
```
This sends !000&; onto the bus to enter pairing mode.

## 🧠 Services (via ESPHome API)
Service	Description
arc_start_discovery	Start periodic discovery broadcast
arc_stop_discovery	Stop discovery loop
arc_query_all	Query all known covers immediately
arc_pair	Send pairing (!000&;)

Accessible from Home Assistant → Developer Tools → Services.

## 🧩 Protocol Details

Standard frame format: !<id><command><data>;

Examples:
!USZr100b180,RA6; → Blind USZ, position 100, RSSI −90 dBm
!USZEnl; → Lost link
!USZEnp; → Not paired

Usable RSSI range: −100 dBm (bad) → −40 dBm (excellent)
Position mapping: ARC 0 = open → HA 1.0, ARC 100 = closed → HA 0.0

## 🧰 Example Dashboard Layout

Home Assistant automatically discovers covers and the pairing button.
Template sensors (RSSI %, Status) can be added to a Lovelace card for live signal and connection monitoring.

## 💡 Known Limitations
- No encrypted ARC+ protocol support (ASCII only)
- Voltage % calculation is optional and user-configurable in ESPHome templates

📄 License

MIT License
© 2025 Redstorm

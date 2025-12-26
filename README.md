# ESPHome ARC Bridge Component

## Status: alpha (discovery + cover control + RSSI + pairing)

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

Protocol reference: Rollease Acmeda “ARC Serial Protocol via ESP32”

## ⚙️ Installation (as External Component)
```
external_components:

source: github://redstorm1/arc-bridge
components: [arc_bridge]
refresh: 1s # optional while iterating to force refetch

uart:
id: arc_uart
tx_pin: GPIO15
rx_pin: GPIO13
baud_rate: 115200
parity: NONE
stop_bits: 1

arc:
id: arc
discovery_on_boot: true
query_interval_ms: 10000

arc_bridge:
  id: arc_bridge
  uart_id: arc_uart
  auto_poll: true           # optional, defaults to true
  auto_poll_interval: 10s   # optional, defaults to 10s
```

### Auto-poll Settings
- `auto_poll`: Enables the bridge’s automatic rotation through covers to refresh their status (default `true`). Set to `false` to disable all background queries; manual commands still work.
- `auto_poll_interval`: Interval between each cover query, provided as any ESPHome time string (default `10s`). Lower values increase responsiveness at the cost of more UART chatter. Setting the interval to `0s` also disables polling.

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

## 📶 Link Quality & Status Sensors

Optionally expose link quality and connection state as individual sensors:
```
sensor:

platform: template
id: lq_usz
name: "Office Blind Link Quality"
unit_of_measurement: "%"
icon: "mdi:signal"

text_sensor:
  - platform: template
    id: status_usz
    name: "Office Blind Status"

  # NEW: power-type sensors
  - platform: template
    id: power_usz
    name: "Office Blind Power"
    icon: "mdi:power-plug"
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

📄 License

MIT License
© 2025 Redstorm

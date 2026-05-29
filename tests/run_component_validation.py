from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


COMMON_CONFIG = """
esphome:
  name: {name}

esp32:
  board: esp32dev
  framework:
    type: {framework}

logger:
  baud_rate: 0

external_components:
  - source:
      type: local
      path: {component_path}
    components: [arc_bridge, arc_bridge_group]

uart:
  - id: rf_a
    rx_pin: GPIO13
    tx_pin: GPIO15
    baud_rate: 115200
    data_bits: 8
    parity: NONE
    stop_bits: 1

arc_bridge:
  id: arc
  uart_id: rf_a
  auto_poll_interval: 30s
  motion_tx_gap: 250ms
  command_retries: 1
  command_retry_timeout: 1500ms
  pairing_status: pairing_status
  last_paired_id: last_paired_id
"""

VALID_CONFIG_BODY = """
button:
  - platform: template
    name: "ARC Pairing"
    entity_category: config
    on_press:
      - lambda: |-
          id(arc)->send_pair_command();

  - platform: template
    name: "ARC Query All"
    entity_category: diagnostic
    on_press:
      - lambda: |-
          id(arc)->send_query_all();
          id(arc)->send_favorite("USZ");
          id(arc)->send_jog_open("USZ");
          id(arc)->send_jog_close("USZ");

cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    name: "Office Blind"
    device_class: shade
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
    id: khn
    name: "Living Blind"
    device_class: shade
    blind_id: "KHN"

  - platform: arc_bridge
    bridge_id: arc
    id: nom
    name: "Living Drape"
    device_class: curtain
    blind_id: "NOM"

  - platform: arc_bridge_group
    id: living_room
    name: "Living Room"
    device_class: shade
    members: [usz, khn]

sensor:
  - platform: template
    id: lq_usz
    name: "Office Blind Link Quality"
    entity_category: diagnostic
    unit_of_measurement: "dBm"
    device_class: signal_strength
  - platform: template
    id: speed_usz
    name: "Office Blind Speed"
    entity_category: diagnostic
    unit_of_measurement: "rpm"
  - platform: template
    id: power_usz
    name: "Office Blind Voltage"
    entity_category: diagnostic
    unit_of_measurement: "V"
    accuracy_decimals: 2
  - platform: template
    id: battery_usz
    name: "Office Blind Battery"
    unit_of_measurement: "%"
    accuracy_decimals: 0
    device_class: battery

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
    id: version_usz
    name: "Office Blind Version"
    entity_category: diagnostic
  - platform: template
    id: limits_usz
    name: "Office Blind Limits"
    entity_category: config
"""

BATTERY_ONLY_BODY = """
cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    name: "Office Blind"
    device_class: shade
    blind_id: "USZ"
    battery_level: battery_usz

sensor:
  - platform: template
    id: battery_usz
    name: "Office Blind Battery"
    unit_of_measurement: "%"
    accuracy_decimals: 0
    device_class: battery

text_sensor:
  - platform: template
    id: pairing_status
    name: "ARC Pairing Status"
    entity_category: diagnostic
  - platform: template
    id: last_paired_id
    name: "ARC Last Paired ID"
    entity_category: diagnostic
"""

INVALID_GROUP_EMPTY_BODY = """
cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    name: "Office Blind"
    device_class: shade
    blind_id: "USZ"

  - platform: arc_bridge_group
    id: living_room
    name: "Living Room"
    members: []

text_sensor:
  - platform: template
    id: pairing_status
    name: "ARC Pairing Status"
    entity_category: diagnostic
  - platform: template
    id: last_paired_id
    name: "ARC Last Paired ID"
    entity_category: diagnostic
"""

INVALID_GROUP_DUPLICATE_BODY = """
cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    name: "Office Blind"
    device_class: shade
    blind_id: "USZ"

  - platform: arc_bridge_group
    id: living_room
    name: "Living Room"
    members: [usz, usz]

text_sensor:
  - platform: template
    id: pairing_status
    name: "ARC Pairing Status"
    entity_category: diagnostic
  - platform: template
    id: last_paired_id
    name: "ARC Last Paired ID"
    entity_category: diagnostic
"""

INVALID_GROUP_NESTED_BODY = """
cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    name: "Office Blind"
    device_class: shade
    blind_id: "USZ"

  - platform: arc_bridge_group
    id: living_room
    name: "Living Room"
    members: [usz]

  - platform: arc_bridge_group
    id: upstairs
    name: "Upstairs"
    members: [living_room]

text_sensor:
  - platform: template
    id: pairing_status
    name: "ARC Pairing Status"
    entity_category: diagnostic
  - platform: template
    id: last_paired_id
    name: "ARC Last Paired ID"
    entity_category: diagnostic
"""

INVALID_GROUP_SELF_BODY = """
cover:
  - platform: arc_bridge
    bridge_id: arc
    id: usz
    name: "Office Blind"
    device_class: shade
    blind_id: "USZ"

  - platform: arc_bridge_group
    id: living_room
    name: "Living Room"
    members: [living_room]

text_sensor:
  - platform: template
    id: pairing_status
    name: "ARC Pairing Status"
    entity_category: diagnostic
  - platform: template
    id: last_paired_id
    name: "ARC Last Paired ID"
    entity_category: diagnostic
"""


def run_esphome(args: list[str], cwd: Path) -> None:
    subprocess.run([sys.executable, "-m", "esphome", *args], check=True, cwd=cwd)


def expect_esphome_failure(args: list[str], cwd: Path) -> None:
    result = subprocess.run([sys.executable, "-m", "esphome", *args], cwd=cwd)
    if result.returncode == 0:
        raise SystemExit(f"Expected failure but command succeeded: {' '.join(args)}")


def write_config(path: Path, name: str, framework: str, component_path: str, body: str) -> None:
    path.write_text(
        COMMON_CONFIG.format(name=name, framework=framework, component_path=component_path) + body,
        encoding="utf-8",
    )


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    component_path = (repo_root / "esphome" / "components").as_posix()

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_root = Path(tmpdir)

        for framework in ("esp-idf", "arduino"):
            name = f"arc-bridge-{framework.replace('-', '')}"
            config_path = tmp_root / f"{name}.yaml"
            write_config(config_path, name, framework, component_path, VALID_CONFIG_BODY)
            run_esphome(["config", str(config_path)], repo_root)
            run_esphome(["compile", str(config_path)], repo_root)

        battery_only_path = tmp_root / "battery-only.yaml"
        write_config(
            battery_only_path,
            "arc-bridge-batteryonly",
            "esp-idf",
            component_path,
            BATTERY_ONLY_BODY,
        )
        run_esphome(["config", str(battery_only_path)], repo_root)

        invalid_configs = {
            "group-empty.yaml": INVALID_GROUP_EMPTY_BODY,
            "group-duplicate.yaml": INVALID_GROUP_DUPLICATE_BODY,
            "group-nested.yaml": INVALID_GROUP_NESTED_BODY,
            "group-self.yaml": INVALID_GROUP_SELF_BODY,
        }

        for filename, body in invalid_configs.items():
            config_path = tmp_root / filename
            write_config(config_path, filename.replace(".yaml", ""), "esp-idf", component_path, body)
            expect_esphome_failure(["config", str(config_path)], repo_root)


if __name__ == "__main__":
    main()

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, cover, sensor, text_sensor
from esphome.const import CONF_ID, CONF_NAME

DEPENDENCIES = ["uart"]

# ──────────────────────────────────────────────
# Namespace and Classes
# ──────────────────────────────────────────────
arc_ns = cg.esphome_ns.namespace("arc_bridge")
ARCBridgeComponent = arc_ns.class_("ARCBridgeComponent", cg.Component, uart.UARTDevice)
ARCBlind = arc_ns.class_("ARCBlind", cover.Cover, cg.Component)

# ──────────────────────────────────────────────
# Config keys
# ──────────────────────────────────────────────
CONF_BLINDS = "blinds"
CONF_BLIND_ID = "blind_id"

# ──────────────────────────────────────────────
# Schema for each blind
# ──────────────────────────────────────────────
BLIND_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ARCBlind),
            cv.Required(CONF_BLIND_ID): cv.string,
            cv.Required(CONF_NAME): cv.string,
        }
    )
    # inherit standard Cover entity schema (adds device_class, icon, disabled_by_default, etc.)
    .extend(cover.COVER_SCHEMA)
)

# ──────────────────────────────────────────────
# Main component schema
# ──────────────────────────────────────────────
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ARCBridgeComponent),
            cv.Optional(CONF_BLINDS, default=[]): cv.ensure_list(BLIND_SCHEMA),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

# ──────────────────────────────────────────────
# Code generation
# ──────────────────────────────────────────────
async def to_code(config):
    # Create the main ARC bridge component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Iterate through all configured blinds
    for blind_cfg in config.get(CONF_BLINDS, []):
        bid = blind_cfg[CONF_BLIND_ID]
        name = blind_cfg[CONF_NAME]

        # Create ARCBlind instance (proper typed ID)
        blind = cg.new_Pvariable(blind_cfg[CONF_ID])
        await cg.register_component(blind, blind_cfg)
        await cover.register_cover(blind, blind_cfg)

        # Wire blind properties and registration
        cg.add(blind.set_blind_id(bid))
        cg.add(blind.set_name(name))
        cg.add(var.add_blind(blind))

        # RF Quality Sensor
        lq = cg.new_Pvariable(sensor.Sensor)
        await sensor.register_sensor(
            lq, {"name": f"{name} RF Quality", "unit_of_measurement": "%"}
        )
        cg.add(var.map_lq_sensor(bid, lq))

        # Status Text Sensor
        status = cg.new_Pvariable(text_sensor.TextSensor)
        await text_sensor.register_text_sensor(status, {"name": f"{name} Status"})
        cg.add(var.map_status_sensor(bid, status))

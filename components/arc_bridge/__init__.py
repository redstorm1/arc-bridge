import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, cover, sensor, text_sensor
from esphome.const import CONF_ID, CONF_NAME

DEPENDENCIES = ["uart"]

arc_ns = cg.esphome_ns.namespace("arc_bridge")
ARCBridgeComponent = arc_ns.class_("ARCBridgeComponent", cg.Component, uart.UARTDevice)
ARCBlind = arc_ns.class_("ARCBlind", cover.Cover, cg.Component)

CONF_BLINDS = "blinds"
CONF_BLIND_ID = "blind_id"

BLIND_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ARCBlind),
            cv.Required(CONF_BLIND_ID): cv.string,
            cv.Required(CONF_NAME): cv.string,
        }
    )
    .extend(cover.COVER_SCHEMA)
)

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


async def to_code(config):
    # Create main ARC bridge component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Iterate over each configured blind
    for blind_cfg in config.get(CONF_BLINDS, []):
        bid = blind_cfg[CONF_BLIND_ID]
        name = blind_cfg[CONF_NAME]

        # Create ARCBlind (Cover already inherits Component)
        blind = cg.new_Pvariable(blind_cfg[CONF_ID])
        await cover.register_cover(blind, blind_cfg)

        cg.add(blind.set_blind_id(bid))
        cg.add(blind.set_name(name))
        cg.add(var.add_blind(blind))

        # --- RF Quality Sensor ---
        lq_config = {
            CONF_ID: cg.new_id(f"{bid}_lq_sensor"),
            "name": f"{name} RF Quality",
            "unit_of_measurement": "%",
            "accuracy_decimals": 0,
        }
        lq = await sensor.new_sensor(lq_config)
        cg.add(var.map_lq_sensor(bid, lq))

        # --- Status Text Sensor ---
        status_config = {
            CONF_ID: cg.new_id(f"{bid}_status_sensor"),
            "name": f"{name} Status",
        }
        status = await text_sensor.new_text_sensor(status_config)
        cg.add(var.map_status_sensor(bid, status))

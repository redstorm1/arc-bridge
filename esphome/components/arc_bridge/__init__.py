import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover, uart, sensor, text_sensor
from esphome.const import CONF_ID, CONF_NAME

DEPENDENCIES = ["uart", "cover", "sensor", "text_sensor"]
MULTI_CONF = True

CONF_BLINDS = "blinds"
CONF_BLIND_ID = "blind_id"

arc_bridge_ns = cg.esphome_ns.namespace("arc_bridge")
ARCBridgeComponent = arc_bridge_ns.class_("ARCBridgeComponent", cg.Component, uart.UARTDevice)
ARCBlind = arc_bridge_ns.class_("ARCBlind", cover.Cover, cg.Component)

BLIND_SCHEMA = cover.cover_schema(ARCBlind).extend(
    {
        cv.Required(CONF_BLIND_ID): cv.string,
        cv.Required(CONF_NAME): cv.string,
        cv.GenerateID(): cv.declare_id(ARCBlind),   # 👈 declare per-blind ID here
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ARCBridgeComponent),
        cv.Optional(CONF_BLINDS): cv.ensure_list(BLIND_SCHEMA),
    }
).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    for blind_cfg in config.get(CONF_BLINDS, []):
        # use the pre-declared ID from YAML validation
        blind = cg.new_Pvariable(blind_cfg[CONF_ID])
        await cg.register_component(blind, blind_cfg)
        await cover.register_cover(blind, blind_cfg)

        cg.add(blind.set_blind_id(blind_cfg[CONF_BLIND_ID]))
        cg.add(blind.set_name(blind_cfg[CONF_NAME]))
        cg.add(var.add_blind(blind))

CONFIG_SCHEMA = CONFIG_SCHEMA
to_code = to_code

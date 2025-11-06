import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, cover
from esphome.const import CONF_ID, CONF_NAME

DEPENDENCIES = ["uart"]

arc_ns = cg.esphome_ns.namespace("arc_bridge")
ARCBridgeComponent = arc_ns.class_("ARCBridgeComponent", cg.Component, uart.UARTDevice)
ARCBlind = arc_ns.class_("ARCBlind", cover.Cover, cg.Component)

CONF_BLINDS = "blinds"
CONF_BLIND_ID = "blind_id"

# ---------------------------------------------------------
# Each blind entry
# ---------------------------------------------------------
BLIND_SCHEMA = cover.cover_schema(ARCBlind).extend(
    {
        cv.Required(CONF_BLIND_ID): cv.string,
        cv.Required(CONF_NAME): cv.string,
    }
)

# ---------------------------------------------------------
# Main component schema
# ---------------------------------------------------------
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

# ---------------------------------------------------------
# Code generation
# ---------------------------------------------------------
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    for blind_cfg in config[CONF_BLINDS]:
        # Create each ARCBlind object
        blind = cg.new_Pvariable(None, ARCBlind)
        await cg.register_component(blind, blind_cfg)
        await cover.register_cover(blind, blind_cfg)

        cg.add(blind.set_blind_id(blind_cfg[CONF_BLIND_ID]))
        cg.add(blind.set_name(blind_cfg[CONF_NAME]))
        cg.add(var.add_blind(blind))

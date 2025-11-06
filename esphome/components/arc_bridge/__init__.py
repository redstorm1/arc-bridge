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
CONF_STATUS_ID = "status_id"

# --- Each blind entry schema ---
BLIND_SCHEMA = cover.cover_schema(ARCBlind).extend(
    {
        cv.Required(CONF_BLIND_ID): cv.string,
        cv.Required(CONF_NAME): cv.string,
        cv.GenerateID(): cv.declare_id(ARCBlind),
        cv.Optional(CONF_STATUS_ID): cv.declare_id(cg.Component),
    }
)

# --- Root component schema ---
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


# --- to_code() codegen routine ---
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    for blind_cfg in config.get(CONF_BLINDS, []):
        bid = blind_cfg[CONF_BLIND_ID]
        name = blind_cfg[CONF_NAME]

        # create the blind cover instance
        blind = cg.new_Pvariable(blind_cfg[CONF_ID])
        await cg.register_component(blind, blind_cfg)
        await cover.register_cover(blind, blind_cfg)

        cg.add(blind.set_blind_id(bid))
        cg.add(blind.set_name(name))
        cg.add(var.add_blind(blind))

        # link quality (LQ) & status handled via map_*
        # (no cg.new_id calls, just pass strings)
        cg.add(var.map_lq_sensor(bid, None))
        cg.add(var.map_status_sensor(bid, None))

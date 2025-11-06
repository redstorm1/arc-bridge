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

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ARCBridgeComponent),
            cv.Optional(CONF_BLINDS, default=[]): cv.ensure_list(
                {
                    cv.Required(CONF_BLIND_ID): cv.string,
                    cv.Required(CONF_NAME): cv.string,
                }
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    for blind_cfg in config.get(CONF_BLINDS, []):
        bid = blind_cfg[CONF_BLIND_ID]
        name = blind_cfg[CONF_NAME]

        # Create cover
        blind = cg.new_Pvariable(f"arc_blind_{bid}", ARCBlind)
        cg.add(blind.set_blind_id(bid))
        cg.add(blind.set_name(name))
        cg.add(var.add_blind(blind))
        await cg.register_component(blind, blind_cfg)
        await cover.register_cover(blind, blind_cfg)

        # RF quality sensor
        lq = cg.new_Pvariable(f"arc_lq_{bid}", sensor.Sensor)
        cg.add(lq.set_name(f"{name} RF Quality"))
        cg.add(lq.set_unit_of_measurement("%"))
        cg.add(var.map_lq_sensor(bid, lq))

        # Status text sensor
        status = cg.new_Pvariable(f"arc_status_{bid}", text_sensor.TextSensor)
        cg.add(status.set_name(f"{name} Status"))
        cg.add(var.map_status_sensor(bid, status))

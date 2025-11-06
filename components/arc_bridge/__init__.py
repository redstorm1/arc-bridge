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
CONF_LQ_ID = "lq_id"
CONF_STATUS_ID = "status_id"

# Each blind has its own Cover (ARCBlind) plus two children (RF quality sensor + status text sensor)
BLIND_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ARCBlind),
            cv.Required(CONF_BLIND_ID): cv.string,
            cv.Required(CONF_NAME): cv.string,
            # Auto-generate IDs for the child entities if the user doesn't provide them
            cv.GenerateID(CONF_LQ_ID): cv.declare_id(sensor.Sensor),
            cv.GenerateID(CONF_STATUS_ID): cv.declare_id(text_sensor.TextSensor),
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
    # Bridge component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # Create each blind + children
    for blind_cfg in config.get(CONF_BLINDS, []):
        bid = blind_cfg[CONF_BLIND_ID]
        name = blind_cfg[CONF_NAME]

        # Cover (inherits Component already)
        blind = cg.new_Pvariable(blind_cfg[CONF_ID])
        await cover.register_cover(blind, blind_cfg)
        cg.add(blind.set_blind_id(bid))
        cg.add(blind.set_name(name))
        cg.add(var.add_blind(blind))

        # RF Quality sensor
        lq = cg.new_Pvariable(blind_cfg[CONF_LQ_ID])
        await sensor.register_sensor(
            lq,
            {
                "name": f"{name} RF Quality",
                "unit_of_measurement": "%",
                "accuracy_decimals": 0,
            },
        )
        cg.add(var.map_lq_sensor(bid, lq))

        # Status text sensor
        status = cg.new_Pvariable(blind_cfg[CONF_STATUS_ID])
        await text_sensor.register_text_sensor(
            status,
            {
                "name": f"{name} Status",
            },
        )
        cg.add(var.map_status_sensor(bid, status))

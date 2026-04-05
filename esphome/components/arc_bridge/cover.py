import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover, sensor, text_sensor
from esphome.const import CONF_BATTERY_LEVEL, CONF_ID, CONF_POWER, CONF_VOLTAGE

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["cover", "sensor", "text_sensor"]

CONF_BRIDGE_ID = "bridge_id"
CONF_BLIND_ID = "blind_id"
CONF_LINK_QUALITY = "link_quality"
CONF_STATUS = "status"
CONF_VERSION = "version"
CONF_SPEED = "speed"
CONF_LIMITS = "limits"
CONF_INVERT_POSITION = "invert_position"

arc_bridge_ns = cg.esphome_ns.namespace("arc_bridge")
ARCBridgeComponent = arc_bridge_ns.class_("ARCBridgeComponent", cg.Component)
ARCCover = arc_bridge_ns.class_("ARCCover", cover.Cover)

CONFIG_SCHEMA = cover.cover_schema(ARCCover).extend(
    {
        cv.GenerateID(): cv.declare_id(ARCCover),
        cv.Required(CONF_BRIDGE_ID): cv.use_id(ARCBridgeComponent),
        cv.Required(CONF_BLIND_ID): cv.string,
        cv.Optional(CONF_LINK_QUALITY): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_STATUS): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_VERSION): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_SPEED): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_LIMITS): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_BATTERY_LEVEL): cv.use_id(sensor.Sensor),
        cv.Exclusive(CONF_POWER, "voltage_sensor"): cv.use_id(sensor.Sensor),
        cv.Exclusive(CONF_VOLTAGE, "voltage_sensor"): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_INVERT_POSITION, default=False): cv.boolean,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cover.register_cover(var, config)

    bridge = await cg.get_variable(config[CONF_BRIDGE_ID])
    cg.add(var.set_bridge(bridge))
    cg.add(var.set_blind_id(config[CONF_BLIND_ID]))
    cg.add(bridge.register_cover(config[CONF_BLIND_ID], var))

    if CONF_INVERT_POSITION in config:
        cg.add(var.set_invert_position(config[CONF_INVERT_POSITION]))

    if CONF_LINK_QUALITY in config:
        lq = await cg.get_variable(config[CONF_LINK_QUALITY])
        cg.add(bridge.map_lq_sensor(config[CONF_BLIND_ID], lq))

    if CONF_STATUS in config:
        st = await cg.get_variable(config[CONF_STATUS])
        cg.add(bridge.map_status_sensor(config[CONF_BLIND_ID], st))

    if CONF_VERSION in config:
        version_sensor = await cg.get_variable(config[CONF_VERSION])
        cg.add(bridge.map_version_sensor(config[CONF_BLIND_ID], version_sensor))

    if CONF_SPEED in config:
        speed_sensor = await cg.get_variable(config[CONF_SPEED])
        cg.add(bridge.map_speed_sensor(config[CONF_BLIND_ID], speed_sensor))

    if CONF_LIMITS in config:
        limits_sensor = await cg.get_variable(config[CONF_LIMITS])
        cg.add(bridge.map_limits_sensor(config[CONF_BLIND_ID], limits_sensor))

    voltage_sensor_id = config.get(CONF_VOLTAGE, config.get(CONF_POWER))
    if voltage_sensor_id is not None:
        voltage_sensor = await cg.get_variable(voltage_sensor_id)
        cg.add(bridge.map_voltage_sensor(config[CONF_BLIND_ID], voltage_sensor))

    if CONF_BATTERY_LEVEL in config:
        battery_sensor = await cg.get_variable(config[CONF_BATTERY_LEVEL])
        cg.add(bridge.map_battery_level_sensor(config[CONF_BLIND_ID], battery_sensor))

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor, uart

CONF_AUTO_POLL = "auto_poll"
CONF_AUTO_POLL_INTERVAL = "auto_poll_interval"
CONF_COMMAND_RETRIES = "command_retries"
CONF_COMMAND_RETRY_TIMEOUT = "command_retry_timeout"
CONF_MOTION_TX_GAP = "motion_tx_gap"
CONF_PAIRING_STATUS = "pairing_status"
CONF_LAST_PAIRED_ID = "last_paired_id"

arc_bridge_ns = cg.esphome_ns.namespace("arc_bridge")
ARCBridgeComponent = arc_bridge_ns.class_("ARCBridgeComponent", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ARCBridgeComponent),
            cv.Optional(CONF_AUTO_POLL, default=True): cv.boolean,
            cv.Optional(CONF_AUTO_POLL_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MOTION_TX_GAP, default="200ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_COMMAND_RETRIES, default=1): cv.int_range(min=0, max=5),
            cv.Optional(
                CONF_COMMAND_RETRY_TIMEOUT, default="1500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PAIRING_STATUS): cv.use_id(text_sensor.TextSensor),
            cv.Optional(CONF_LAST_PAIRED_ID): cv.use_id(text_sensor.TextSensor),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[cv.CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_auto_poll_enabled(config[CONF_AUTO_POLL]))
    interval = config[CONF_AUTO_POLL_INTERVAL]
    cg.add(var.set_auto_poll_interval(interval.total_milliseconds))
    motion_gap = config[CONF_MOTION_TX_GAP]
    cg.add(var.set_motion_tx_gap(motion_gap.total_milliseconds))
    cg.add(var.set_command_retry_count(config[CONF_COMMAND_RETRIES]))
    retry_timeout = config[CONF_COMMAND_RETRY_TIMEOUT]
    cg.add(var.set_command_retry_timeout(retry_timeout.total_milliseconds))

    if CONF_PAIRING_STATUS in config:
        pairing_status = await cg.get_variable(config[CONF_PAIRING_STATUS])
        cg.add(var.set_pairing_status_sensor(pairing_status))

    if CONF_LAST_PAIRED_ID in config:
        last_paired_id = await cg.get_variable(config[CONF_LAST_PAIRED_ID])
        cg.add(var.set_last_paired_id_sensor(last_paired_id))

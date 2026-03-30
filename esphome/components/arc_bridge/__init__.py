import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart

CONF_AUTO_POLL = "auto_poll"
CONF_AUTO_POLL_INTERVAL = "auto_poll_interval"
CONF_COMMAND_RETRIES = "command_retries"
CONF_COMMAND_RETRY_TIMEOUT = "command_retry_timeout"

arc_bridge_ns = cg.esphome_ns.namespace("arc_bridge")
ARCBridgeComponent = arc_bridge_ns.class_("ARCBridgeComponent", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ARCBridgeComponent),
            cv.Optional(CONF_AUTO_POLL, default=True): cv.boolean,
            cv.Optional(CONF_AUTO_POLL_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_COMMAND_RETRIES, default=1): cv.int_range(min=0, max=5),
            cv.Optional(
                CONF_COMMAND_RETRY_TIMEOUT, default="1500ms"
            ): cv.positive_time_period_milliseconds,
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
    cg.add(var.set_command_retry_count(config[CONF_COMMAND_RETRIES]))
    retry_timeout = config[CONF_COMMAND_RETRY_TIMEOUT]
    cg.add(var.set_command_retry_timeout(retry_timeout.total_milliseconds))

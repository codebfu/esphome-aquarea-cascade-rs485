import esphome.codegen as cg
from esphome.components import text_sensor, uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIMEOUT

CODEOWNERS = ["@codebfu"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["text_sensor"]
MULTI_CONF = False

CONF_AQUAREA_CASCADE_ID = "aquarea_cascade_id"
CONF_IDLE_TIME = "idle_time"
CONF_ADDRESS = "address"

aquarea_cascade_ns = cg.esphome_ns.namespace("aquarea_cascade")
AquareaCascade = aquarea_cascade_ns.class_("AquareaCascade", cg.Component, uart.UARTDevice)
Command = aquarea_cascade_ns.enum("Command", is_class=True)

COMMANDS = {
    "off": Command.OFF,
    "heat": Command.HEAT,
    "cool": Command.COOL,
    "ecs": Command.ECS,
    "heat_ecs": Command.HEAT_ECS,
    "cool_ecs": Command.COOL_ECS,
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AquareaCascade),
            cv.Optional(CONF_ADDRESS, default=1): cv.int_range(min=1, max=247),
            cv.Optional(CONF_IDLE_TIME, default="180ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TIMEOUT, default="60s"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_idle_ms(config[CONF_IDLE_TIME].total_milliseconds))
    cg.add(var.set_confirm_timeout_ms(config[CONF_TIMEOUT].total_milliseconds))

"""ESPHome component for the SpacePi X4L with custom firmware (UART protocol 1.3.0 or later)."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["climate"]
MULTI_CONF = False

CONF_X4L_ID = "x4l_id"

x4l_ns = cg.esphome_ns.namespace("x4l")
X4LComponent = x4l_ns.class_("X4LComponent", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(X4LComponent)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema("x4l", baud_rate=115200, require_rx=True, require_tx=True)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

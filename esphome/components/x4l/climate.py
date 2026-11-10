"""Thermostat per chamber: mode heat/off = start/stop drying, target temperature 35-75 °C."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate

from . import CONF_X4L_ID, X4LComponent, x4l_ns

DEPENDENCIES = ["x4l"]
CONF_CHAMBER = "chamber"

X4LClimate = x4l_ns.class_("X4LClimate", climate.Climate, cg.Component)

CONFIG_SCHEMA = (
    climate.climate_schema(X4LClimate)
    .extend(
        {
            cv.GenerateID(CONF_X4L_ID): cv.use_id(X4LComponent),
            cv.Required(CONF_CHAMBER): cv.one_of("left", "right", lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_X4L_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_chamber(0 if config[CONF_CHAMBER] == "left" else 1))

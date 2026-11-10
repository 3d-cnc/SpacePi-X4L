"""Thermostat je Kammer: Modus Heizen/Aus = Trocknen starten/stoppen, Solltemperatur 35-75 °C."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate

from . import CONF_X4L_ID, X4LComponent, x4l_ns

DEPENDENCIES = ["x4l"]
CONF_KAMMER = "kammer"

X4LClimate = x4l_ns.class_("X4LClimate", climate.Climate, cg.Component)

CONFIG_SCHEMA = (
    climate.climate_schema(X4LClimate)
    .extend(
        {
            cv.GenerateID(CONF_X4L_ID): cv.use_id(X4LComponent),
            cv.Required(CONF_KAMMER): cv.one_of("links", "rechts", lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_X4L_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_kammer(0 if config[CONF_KAMMER] == "links" else 1))

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_AQUAREA_CASCADE_ID, AquareaCascade, aquarea_cascade_ns

DEPENDENCIES = ["aquarea_cascade"]

CONF_TYPE = "type"

AquareaCascadeTextSensor = aquarea_cascade_ns.class_(
    "AquareaCascadeTextSensor", text_sensor.TextSensor, cg.Component
)

TYPES = {
    "result": "set_result_sensor",
    "phase": "set_phase_sensor",
    "reset_reason": "set_reset_reason_sensor",
}

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(
        AquareaCascadeTextSensor, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    )
    .extend(
        {
            cv.GenerateID(CONF_AQUAREA_CASCADE_ID): cv.use_id(AquareaCascade),
            cv.Optional(CONF_TYPE, default="result"): cv.enum(TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQUAREA_CASCADE_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await text_sensor.register_text_sensor(var, config)
    await cg.register_component(var, config)
    cg.add(getattr(parent, TYPES[config[CONF_TYPE]])(var))

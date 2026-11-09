import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_AQUAREA_CASCADE_ID, AquareaCascade, aquarea_cascade_ns

DEPENDENCIES = ["aquarea_cascade"]

AquareaCascadeResultSensor = aquarea_cascade_ns.class_(
    "AquareaCascadeResultSensor", text_sensor.TextSensor, cg.Component
)

CONFIG_SCHEMA = text_sensor.text_sensor_schema(AquareaCascadeResultSensor).extend(
    {
        cv.GenerateID(CONF_AQUAREA_CASCADE_ID): cv.use_id(AquareaCascade),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQUAREA_CASCADE_ID])
    var = cg.new_Pvariable(config[CONF_ID])
    await text_sensor.register_text_sensor(var, config)
    await cg.register_component(var, config)
    cg.add(parent.set_result_sensor(var))

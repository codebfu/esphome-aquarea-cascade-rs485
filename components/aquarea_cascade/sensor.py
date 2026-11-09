import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
)

from . import CONF_AQUAREA_CASCADE_ID, AquareaCascade, aquarea_cascade_ns

DEPENDENCIES = ["aquarea_cascade"]

CONF_TYPE = "type"

TYPES = {
    "sniff_op": "set_sniff_op_sensor",
    "sniff_mode": "set_sniff_mode_sensor",
    "sniff_tank": "set_sniff_tank_sensor",
    "rx_frames": "set_rx_frames_sensor",
}

AquareaCascadeSensor = aquarea_cascade_ns.class_(
    "AquareaCascadeSensor", sensor.Sensor, cg.Component
)

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        AquareaCascadeSensor,
        accuracy_decimals=0,
        state_class=STATE_CLASS_MEASUREMENT,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )
    .extend(
        {
            cv.GenerateID(CONF_AQUAREA_CASCADE_ID): cv.use_id(AquareaCascade),
            cv.Required(CONF_TYPE): cv.enum(TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQUAREA_CASCADE_ID])
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    cg.add(getattr(parent, TYPES[config[CONF_TYPE]])(var))

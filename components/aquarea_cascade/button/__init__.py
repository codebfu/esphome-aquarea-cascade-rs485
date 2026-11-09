import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import COMMANDS, CONF_AQUAREA_CASCADE_ID, AquareaCascade, aquarea_cascade_ns

DEPENDENCIES = ["aquarea_cascade"]

CONF_COMMAND = "command"

AquareaCascadeButton = aquarea_cascade_ns.class_("AquareaCascadeButton", button.Button, cg.Component)

CONFIG_SCHEMA = (
    button.button_schema(AquareaCascadeButton)
    .extend(
        {
            cv.GenerateID(CONF_AQUAREA_CASCADE_ID): cv.use_id(AquareaCascade),
            cv.Required(CONF_COMMAND): cv.enum(COMMANDS, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_AQUAREA_CASCADE_ID])
    var = await button.new_button(config)
    await cg.register_component(var, config)
    cg.add(var.set_parent(parent))
    cg.add(var.set_command(config[CONF_COMMAND]))

import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

from . import (
    CONF_VRC720_ID,
    VALUE_TYPE_MAP,
    VRC720Button,
    apply_entity_defaults,
    inject_default_name,
    instance_of,
    register_schema,
    validate_register,
    write_instance_of,
)
from . import registers

DEPENDENCIES = ["vaillant_vrc720"]

_PLATFORM_SCHEMA = button.button_schema(VRC720Button).extend(register_schema(registers.BUTTON))
_PLATFORM_SCHEMA.prepend_extra(inject_default_name(registers.BUTTON))

CONFIG_SCHEMA = cv.All(_PLATFORM_SCHEMA, validate_register(registers.BUTTON))


async def to_code(config: ConfigType) -> None:
    entry = registers.REGISTERS[config["register"]]
    instance = instance_of(config, entry)
    apply_entity_defaults(config, entry, instance)

    var = cg.new_Pvariable(config[CONF_ID])
    hub = await cg.get_variable(config[CONF_VRC720_ID])
    # Buttons are write-only commands: no read subscription.
    cg.add(
        var.set_vrc720_parent(
            hub, entry["op"], entry["gg"], entry["rr"], instance, VALUE_TYPE_MAP[entry["value_type"]], False
        )
    )
    cg.add(var.set_write_config(write_instance_of(entry, instance), entry["button_value"][0]))
    await button.register_button(var, config)

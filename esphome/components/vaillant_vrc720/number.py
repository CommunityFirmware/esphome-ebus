import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP
from esphome.types import ConfigType

from . import (
    CONF_VRC720_ID,
    VALUE_TYPE_MAP,
    WRITE_ENCODING_MAP,
    VRC720Number,
    apply_entity_defaults,
    inject_default_name,
    instance_of,
    register_schema,
    validate_register,
    write_instance_of,
)
from . import registers

DEPENDENCIES = ["vaillant_vrc720"]


def inject_number_defaults(config):
    """Fill in the catalog min/max/step for numbers and validate the range."""
    entry = registers.REGISTERS[config["register"]]
    config.setdefault(CONF_MIN_VALUE, entry["min_value"])
    config.setdefault(CONF_MAX_VALUE, entry["max_value"])
    config.setdefault(CONF_STEP, entry["step"])
    if config[CONF_MAX_VALUE] <= config[CONF_MIN_VALUE]:
        raise cv.Invalid("max_value must be greater than min_value", path=[CONF_MAX_VALUE])
    return config


_PLATFORM_SCHEMA = (
    number.number_schema(VRC720Number)
    .extend(register_schema(registers.NUMBER))
    .extend(
        {
            cv.Optional(CONF_MIN_VALUE): cv.float_,
            cv.Optional(CONF_MAX_VALUE): cv.float_,
            cv.Optional(CONF_STEP): cv.positive_float,
        }
    )
)
_PLATFORM_SCHEMA.prepend_extra(inject_default_name(registers.NUMBER))

CONFIG_SCHEMA = cv.All(
    _PLATFORM_SCHEMA,
    validate_register(registers.NUMBER),
    inject_number_defaults,
)


async def to_code(config: ConfigType) -> None:
    entry = registers.REGISTERS[config["register"]]
    instance = instance_of(config, entry)
    apply_entity_defaults(config, entry, instance)

    var = cg.new_Pvariable(config[CONF_ID])
    hub = await cg.get_variable(config[CONF_VRC720_ID])
    cg.add(
        var.set_vrc720_parent(
            hub, entry["op"], entry["gg"], entry["rr"], instance, VALUE_TYPE_MAP[entry["value_type"]]
        )
    )
    cg.add(
        var.set_write_config(
            write_instance_of(entry, instance), WRITE_ENCODING_MAP[entry["write_encoding"]]
        )
    )
    await number.register_number(
        var,
        config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

from . import (
    CONF_VRC720_ID,
    VALUE_TYPE_MAP,
    VRC720BinarySensor,
    apply_entity_defaults,
    inject_default_name,
    instance_of,
    register_schema,
    validate_register,
)
from . import registers

DEPENDENCIES = ["vaillant_vrc720"]

_PLATFORM_SCHEMA = binary_sensor.binary_sensor_schema(VRC720BinarySensor).extend(
    register_schema(registers.BINARY_SENSOR)
)
_PLATFORM_SCHEMA.prepend_extra(inject_default_name(registers.BINARY_SENSOR))

CONFIG_SCHEMA = cv.All(_PLATFORM_SCHEMA, validate_register(registers.BINARY_SENSOR))


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
    if entry.get("inverted"):
        cg.add(var.set_inverted(True))
    await binary_sensor.register_binary_sensor(var, config)

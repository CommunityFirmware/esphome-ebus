import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

from . import (
    CONF_VRC720_ID,
    VALUE_TYPE_MAP,
    VRC720TextSensor,
    apply_entity_defaults,
    inject_default_name,
    instance_of,
    register_schema,
    validate_register,
)
from . import registers

DEPENDENCIES = ["vaillant_vrc720"]

_PLATFORM_SCHEMA = text_sensor.text_sensor_schema(VRC720TextSensor).extend(
    register_schema(registers.TEXT_SENSOR)
)
_PLATFORM_SCHEMA.prepend_extra(inject_default_name(registers.TEXT_SENSOR))

CONFIG_SCHEMA = cv.All(_PLATFORM_SCHEMA, validate_register(registers.TEXT_SENSOR))


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
    await text_sensor.register_text_sensor(var, config)

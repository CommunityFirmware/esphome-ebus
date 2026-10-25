import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_STATE_CLASS
from esphome.types import ConfigType

from . import (
    CONF_VRC720_ID,
    VALUE_TYPE_MAP,
    VRC720Sensor,
    apply_entity_defaults,
    inject_default_name,
    instance_of,
    register_schema,
    validate_register,
)
from . import registers

DEPENDENCIES = ["vaillant_vrc720"]

_PLATFORM_SCHEMA = sensor.sensor_schema(VRC720Sensor).extend(register_schema(registers.SENSOR))
_PLATFORM_SCHEMA.prepend_extra(inject_default_name(registers.SENSOR))

CONFIG_SCHEMA = cv.All(_PLATFORM_SCHEMA, validate_register(registers.SENSOR))


async def to_code(config: ConfigType) -> None:
    entry = registers.REGISTERS[config["register"]]
    instance = instance_of(config, entry)
    apply_entity_defaults(config, entry, instance)
    if entry["state_class"] is not None:
        # Run the catalog value through the schema validator so the config holds
        # an EnumValue (str subclass) instead of a raw MockObj enum.
        config.setdefault(CONF_STATE_CLASS, sensor.validate_state_class(entry["state_class"]))

    var = cg.new_Pvariable(config[CONF_ID])
    hub = await cg.get_variable(config[CONF_VRC720_ID])
    cg.add(
        var.set_vrc720_parent(
            hub, entry["op"], entry["gg"], entry["rr"], instance, VALUE_TYPE_MAP[entry["value_type"]]
        )
    )
    await sensor.register_sensor(var, config)

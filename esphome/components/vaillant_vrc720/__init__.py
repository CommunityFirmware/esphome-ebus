import esphome.codegen as cg
from esphome.components import binary_sensor, button, climate, ebus, number, select, sensor, text_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ACCURACY_DECIMALS,
    CONF_ADDRESS,
    CONF_DEVICE_CLASS,
    CONF_ENTITY_CATEGORY,
    CONF_ICON,
    CONF_ID,
    CONF_NAME,
    CONF_UNIT_OF_MEASUREMENT,
)

from . import registers

DOMAIN = "vaillant_vrc720"

CODEOWNERS = ["@burmistrzak"]

DEPENDENCIES = ["ebus"]
MULTI_CONF = True

CONF_VRC720_ID = "vrc720_id"
CONF_EBUS_ID = "ebus_id"
CONF_REGISTER = "register"
CONF_CIRCUIT = "circuit"
CONF_ZONE = "zone"
CONF_DEVICE = "device"

DEFAULT_VRC720_ADDRESS = 0x15

vaillant_ns = cg.esphome_ns.namespace("vaillant")
VaillantVRC720 = vaillant_ns.class_("VaillantVRC720", cg.PollingComponent)
B524ValueType = vaillant_ns.enum("B524ValueType", is_class=True)
B524WriteEncoding = vaillant_ns.enum("B524WriteEncoding", is_class=True)

VRC720Sensor = vaillant_ns.class_("VRC720Sensor", sensor.Sensor)
VRC720BinarySensor = vaillant_ns.class_("VRC720BinarySensor", binary_sensor.BinarySensor)
VRC720Number = vaillant_ns.class_("VRC720Number", number.Number)
VRC720Select = vaillant_ns.class_("VRC720Select", select.Select)
VRC720TextSensor = vaillant_ns.class_("VRC720TextSensor", text_sensor.TextSensor)
VRC720Button = vaillant_ns.class_("VRC720Button", button.Button)
VRC720Climate = vaillant_ns.class_("VRC720Climate", climate.Climate, cg.Component)

VALUE_TYPE_MAP = {
    registers.F32: B524ValueType.F32,
    registers.U16: B524ValueType.U16,
    registers.U8: B524ValueType.U8,
    registers.I8: B524ValueType.I8,
    registers.U32: B524ValueType.U32,
    registers.BOOL: B524ValueType.BOOL,
    registers.STRING: B524ValueType.STRING,
    registers.ENUM16: B524ValueType.ENUM16,
    registers.DATE: B524ValueType.DATE,
    registers.TIME: B524ValueType.TIME,
    registers.VERSION: B524ValueType.VERSION,
    registers.RAW: B524ValueType.RAW,
}

WRITE_ENCODING_MAP = {
    registers.WRITE_F32: B524WriteEncoding.F32,
    registers.WRITE_U16: B524WriteEncoding.U16,
    registers.WRITE_U8: B524WriteEncoding.U8,
}

_INSTANCE_KEYS = (CONF_CIRCUIT, CONF_ZONE, CONF_DEVICE)


def validate_target_address(value):
    value = cv.hex_uint8_t(value)
    hi = (value >> 4) & 0x0F
    lo = value & 0x0F
    master_nibbles = (0x0, 0x1, 0x3, 0x7, 0xF)
    if hi in master_nibbles and lo in master_nibbles:
        raise cv.Invalid(
            f"Invalid device address 0x{value:02X}: both nibbles are initiator nibbles "
            "(0x0, 0x1, 0x3, 0x7, 0xF); a target device needs a slave address such as 0x15"
        )
    if value == 0xFE:
        raise cv.Invalid("Invalid device address 0xFE: 0xFE is the eBUS broadcast address")
    if value in (0xAA, 0xA9):
        raise cv.Invalid(f"Invalid device address 0x{value:02X}: reserved eBUS symbol")
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VaillantVRC720),
        cv.Required(CONF_EBUS_ID): cv.use_id(ebus.EBUSHub),
        cv.Optional(CONF_ADDRESS, default=DEFAULT_VRC720_ADDRESS): validate_target_address,
    }
).extend(cv.polling_component_schema("30s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    hub = await cg.get_variable(config[CONF_EBUS_ID])
    cg.add(var.set_ebus_hub(hub))
    cg.add(var.set_address(config[CONF_ADDRESS]))


# ---- shared helpers for the entity platforms --------------------------------


def register_names(entity_kind):
    return sorted(name for name, entry in registers.REGISTERS.items() if entry["entity_kind"] == entity_kind)


def register_schema(entity_kind):
    """Common platform schema: hub reference, register name, instance keys."""
    return cv.Schema(
        {
            cv.GenerateID(CONF_VRC720_ID): cv.use_id(VaillantVRC720),
            cv.Required(CONF_REGISTER): cv.one_of(*register_names(entity_kind)),
            cv.Optional(CONF_CIRCUIT): cv.int_range(min=0, max=9),
            cv.Optional(CONF_ZONE): cv.int_range(min=0, max=0x0A),
            cv.Optional(CONF_DEVICE): cv.int_range(min=1, max=8),
        }
    )


def validate_register(entity_kind):
    """Cross-validate the register name against the platform and instance keys."""

    def validator(config):
        name = config[CONF_REGISTER]
        entry = registers.REGISTERS[name]
        if entry["entity_kind"] != entity_kind:
            raise cv.Invalid(
                f"Register '{name}' is a {entry['entity_kind']} register, not a {entity_kind} register",
                path=[CONF_REGISTER],
            )
        instance_kind = entry["instance_kind"]
        if instance_kind == registers.INSTANCE_NONE:
            for key in _INSTANCE_KEYS:
                if key in config:
                    raise cv.Invalid(f"Register '{name}' is not instanced; remove '{key}'", path=[key])
            return config
        key = registers.INSTANCE_KEYS[instance_kind]
        if key not in config:
            raise cv.Invalid(f"Register '{name}' requires '{key}'", path=[key])
        for other in _INSTANCE_KEYS:
            if other != key and other in config:
                raise cv.Invalid(f"Register '{name}' takes '{key}', not '{other}'", path=[other])
        lo, hi = registers.INSTANCE_RANGES[instance_kind]
        if not lo <= config[key] <= hi:
            raise cv.Invalid(f"'{key}' must be in the range {lo}..{hi} for register '{name}'", path=[key])
        return config

    return validator


def instance_of(config, entry):
    """The read instance (II) for a register: the YAML instance key, or 0."""
    if entry["instance_kind"] == registers.INSTANCE_NONE:
        return 0
    return config.get(registers.INSTANCE_KEYS[entry["instance_kind"]], 0)


def write_instance_of(entry, instance):
    """The write instance (II) for a register: fixed for non-instanced registers,
    the YAML instance for instanced ones."""
    if entry["instance_kind"] == registers.INSTANCE_NONE:
        return entry["write_instance"]
    return instance


def default_name(entry, instance):
    name = entry["name"].replace("_", " ").title()
    # str.title() mangles initialisms used in the register catalog.
    name = name.replace("Dhw", "DHW").replace("Vr71", "VR71")
    if entry["instance_kind"] in (registers.INSTANCE_CIRCUIT, registers.INSTANCE_ZONE):
        return f"{name} {instance + 1}"
    if entry["instance_kind"] == registers.INSTANCE_DEVICE:
        return f"{name} {instance}"
    return name


def inject_default_name(entity_kind):
    """Schema extra (prepended) that derives a default entity name from the
    register, so users can configure an entity with just `register:`."""

    def validator(config):
        entry = registers.REGISTERS[config[CONF_REGISTER]]
        config.setdefault(CONF_NAME, default_name(entry, instance_of(config, entry)))
        return config

    return validator


def apply_entity_defaults(config, entry, instance):
    """Fill in catalog defaults (unit, device class, ...) for keys the user did
    not override. Called from to_code before the entity is registered.
    state_class is sensor-only and needs enum conversion — the sensor platform
    injects it itself."""
    config.setdefault(CONF_NAME, default_name(entry, instance))
    if entry["unit"] is not None:
        config.setdefault(CONF_UNIT_OF_MEASUREMENT, entry["unit"])
    if entry["device_class"] is not None:
        config.setdefault(CONF_DEVICE_CLASS, entry["device_class"])
    if entry["accuracy_decimals"] is not None:
        config.setdefault(CONF_ACCURACY_DECIMALS, entry["accuracy_decimals"])
    if entry["icon"] is not None:
        config.setdefault(CONF_ICON, entry["icon"])
    if entry["entity_category"] is not None:
        config.setdefault(CONF_ENTITY_CATEGORY, entry["entity_category"])
    return config

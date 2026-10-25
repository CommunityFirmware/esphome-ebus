import datetime

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ACCURACY_DECIMALS,
    CONF_DEVICE_CLASS,
    CONF_ID,
    CONF_NAME,
    CONF_STATE_CLASS,
    CONF_UNIT_OF_MEASUREMENT,
)
from esphome.types import ConfigType

from . import (
    CONF_ENERGY_ID,
    EnergySensor,
    VaillantEnergy,
)
from esphome.components.vaillant_vrc720 import validate_target_address

DEPENDENCIES = ["vaillant_energy"]

CONF_STATISTICS = "statistics"
CONF_POWER = "power"
CONF_SOURCE = "source"
CONF_USAGE = "usage"
CONF_PERIOD = "period"
CONF_DATE = "date"
CONF_TARGET_ADDRESS = "address"

# Selector values from the B516 protocol notes (request bytes 4/5 low nibbles).
SOURCES = {"solar": 1, "environmental": 2, "electrical": 3, "fuel": 4}
USAGES = {"sum": 0, "heating": 3, "dhw": 4, "cooling": 5}
PERIODS = {"total": 0, "daily": 1}

SOURCE_NAMES = {"solar": "Solar", "environmental": "Environmental", "electrical": "Electrical", "fuel": "Fuel"}
USAGE_NAMES = {"sum": None, "heating": "Heating", "dhw": "DHW", "cooling": "Cooling"}
PERIOD_NAMES = {"total": "Total", "daily": "Daily"}


def validate_date(value):
    """An explicit calendar date (YYYY-MM-DD) for daily statistics windows."""
    value = cv.string_strict(value)
    try:
        date = datetime.date.fromisoformat(value)
    except ValueError as e:
        raise cv.Invalid(f"Invalid date '{value}': expected YYYY-MM-DD") from e
    return date


def _validate_entity(config):
    if (CONF_STATISTICS in config) == (CONF_POWER in config):
        raise cv.Invalid("Specify exactly one of 'statistics' or 'power'")
    return config


def _inject_default_name(config):
    if CONF_STATISTICS in config:
        stats = config[CONF_STATISTICS]
        parts = [SOURCE_NAMES[stats[CONF_SOURCE]]]
        if USAGE_NAMES[stats[CONF_USAGE]] is not None:
            parts.append(USAGE_NAMES[stats[CONF_USAGE]])
        parts += ["Energy", PERIOD_NAMES[stats[CONF_PERIOD]]]
        config.setdefault(CONF_NAME, " ".join(parts))
    else:
        config.setdefault(CONF_NAME, "Current Electrical Power")
    return config


_STATISTICS_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SOURCE): cv.one_of(*SOURCES, lower=True),
        cv.Required(CONF_USAGE): cv.one_of(*USAGES, lower=True),
        cv.Required(CONF_PERIOD): cv.one_of(*PERIODS, lower=True),
        # Without a date, daily windows use today's date from the hub's time
        # source; an explicit date queries that specific day.
        cv.Optional(CONF_DATE): validate_date,
    }
)

_PLATFORM_SCHEMA = (
    sensor.sensor_schema(EnergySensor)
    .extend(
        {
            cv.GenerateID(CONF_ENERGY_ID): cv.use_id(VaillantEnergy),
            cv.Optional(CONF_TARGET_ADDRESS): validate_target_address,
            cv.Optional(CONF_STATISTICS): _STATISTICS_SCHEMA,
            cv.Optional(CONF_POWER): cv.boolean,
        }
    )
    .prepend_extra(_inject_default_name)
)

CONFIG_SCHEMA = cv.All(_PLATFORM_SCHEMA, _validate_entity)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_ENERGY_ID])
    cg.add(var.set_parent(parent))
    if CONF_TARGET_ADDRESS in config:
        cg.add(var.set_target_address(config[CONF_TARGET_ADDRESS]))

    if CONF_STATISTICS in config:
        stats = config[CONF_STATISTICS]
        period = PERIODS[stats[CONF_PERIOD]]
        source = SOURCES[stats[CONF_SOURCE]]
        usage = USAGES[stats[CONF_USAGE]]
        if CONF_DATE in stats:
            date = stats[CONF_DATE]
            cg.add(var.set_statistics(period, source, usage, date.year, date.month, date.day, True))
        else:
            cg.add(var.set_statistics(period, source, usage, 0, 0, 0, False))
        config.setdefault(CONF_UNIT_OF_MEASUREMENT, "kWh")
        config.setdefault(CONF_DEVICE_CLASS, "energy")
        config.setdefault(CONF_STATE_CLASS, sensor.validate_state_class("total_increasing"))
        config.setdefault(CONF_ACCURACY_DECIMALS, 2)
    else:
        cg.add(var.set_power())
        config.setdefault(CONF_UNIT_OF_MEASUREMENT, "W")
        config.setdefault(CONF_DEVICE_CLASS, "power")
        config.setdefault(CONF_STATE_CLASS, sensor.validate_state_class("measurement"))
        config.setdefault(CONF_ACCURACY_DECIMALS, 0)

    await sensor.register_sensor(var, config)

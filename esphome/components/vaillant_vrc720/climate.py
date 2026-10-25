import esphome.codegen as cg
from esphome.components import climate
import esphome.config_validation as cv
from esphome.const import CONF_ICON, CONF_ID, CONF_NAME
from esphome.types import ConfigType

from . import (
    CONF_VRC720_ID,
    CONF_ZONE,
    VRC720Climate,
    VaillantVRC720,
)

DEPENDENCIES = ["vaillant_vrc720"]

CONF_DHW = "dhw"
CONF_QUICK_VETO_DURATION = "quick_veto_duration"


def _validate_target(config):
    if (CONF_ZONE in config) == (CONF_DHW in config):
        raise cv.Invalid("Specify exactly one of 'zone' or 'dhw'")
    if config.get(CONF_DHW) and CONF_QUICK_VETO_DURATION in config:
        raise cv.Invalid("'quick_veto_duration' only applies to zone climates", path=[CONF_QUICK_VETO_DURATION])
    return config


def _apply_defaults(config):
    if CONF_NAME not in config:
        config[CONF_NAME] = "DHW" if config.get(CONF_DHW) else f"Zone {config[CONF_ZONE] + 1}"
    if CONF_ICON not in config and config.get(CONF_DHW):
        config[CONF_ICON] = "mdi:water-boiler"
    return config


_PLATFORM_SCHEMA = climate.climate_schema(VRC720Climate).extend(
    {
        cv.GenerateID(CONF_VRC720_ID): cv.use_id(VaillantVRC720),
        cv.Optional(CONF_ZONE): cv.int_range(min=0, max=0x0A),
        cv.Optional(CONF_DHW): cv.boolean,
        cv.Optional(CONF_QUICK_VETO_DURATION): cv.float_range(min=0.5, max=12),
        cv.Optional(CONF_ICON): cv.icon,
    }
)
_PLATFORM_SCHEMA.prepend_extra(_apply_defaults)

CONFIG_SCHEMA = cv.All(_PLATFORM_SCHEMA, _validate_target)


async def to_code(config: ConfigType) -> None:
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    hub = await cg.get_variable(config[CONF_VRC720_ID])
    zone = config.get(CONF_ZONE, 0)
    dhw = config.get(CONF_DHW, False)
    duration = config.get(CONF_QUICK_VETO_DURATION, 3.0)
    cg.add(var.set_vrc720_parent(hub, zone, dhw, duration))

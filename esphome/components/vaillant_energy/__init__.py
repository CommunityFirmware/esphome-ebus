import esphome.codegen as cg
from esphome.components import ebus, sensor, time
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS, CONF_ID

from esphome.components.vaillant_vrc720 import validate_target_address

DOMAIN = "vaillant_energy"

CODEOWNERS = ["@burmistrzak"]

DEPENDENCIES = ["ebus"]

CONF_EBUS_ID = "ebus_id"
CONF_TIME_ID = "time_id"
CONF_ENERGY_ID = "energy_id"

DEFAULT_ENERGY_ADDRESS = 0x15

vaillant_ns = cg.esphome_ns.namespace("vaillant")
VaillantEnergy = vaillant_ns.class_("VaillantEnergy", cg.PollingComponent)
EnergySensor = vaillant_ns.class_("EnergySensor", sensor.Sensor, cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VaillantEnergy),
        cv.Required(CONF_EBUS_ID): cv.use_id(ebus.EBUSHub),
        cv.Optional(CONF_ADDRESS, default=DEFAULT_ENERGY_ADDRESS): validate_target_address,
        # Time source for "daily" statistics windows (today's date). Without
        # it, daily queries are skipped and a warning is logged.
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    hub = await cg.get_variable(config[CONF_EBUS_ID])
    cg.add(var.set_ebus_hub(hub))
    cg.add(var.set_address(config[CONF_ADDRESS]))
    if CONF_TIME_ID in config:
        time_source = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(time_source))

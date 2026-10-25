import esphome.codegen as cg
from esphome.components import light, uart
import esphome.config_validation as cv
from esphome.const import CONF_ID

DOMAIN = "ebus"

DEPENDENCIES = ["uart"]

CODEOWNERS = ["@burmistrzak"]

CONF_EBUS_ADDRESS = "address"
CONF_LISTEN_ONLY = "listen_only"
CONF_STATUS_LED = "status_led"
CONF_ARBITRATION_DELAY = "arbitration_delay"

DEFAULT_EBUS_ADDRESS = 0x31
# Microseconds to yield after the bus is observed free before starting a
# telegram (range 0-620).
DEFAULT_ARBITRATION_DELAY_US = 200

ebus_ns = cg.esphome_ns.namespace("ebus")
EBUSHub = ebus_ns.class_("EBUSHub", cg.Component, uart.UARTDevice)


def validate_address(value):
    value = cv.hex_uint8_t(value)
    hi = (value >> 4) & 0x0F
    lo = value & 0x0F
    for nibble in (hi, lo):
        if nibble not in (0x0, 0x1, 0x3, 0x7, 0xF):
            raise cv.Invalid(
                f"Invalid eBUS initiator address 0x{value:02X}: both nibbles must be one of "
                "0x0, 0x1, 0x3, 0x7, 0xF (e.g. 0x10, 0x31, 0xF1)"
            )
    return value


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EBUSHub),
        cv.Optional(CONF_EBUS_ADDRESS, default=DEFAULT_EBUS_ADDRESS): validate_address,
        cv.Optional(CONF_LISTEN_ONLY, default=False): cv.boolean,
        # Microseconds to wait after the bus becomes free (SYN or idle)
        # before starting a telegram. Yielding the start of the bus-free
        # phase lets competing initiators win cleanly instead of producing a
        # skewed collision (garbage echo byte, retry). 0 disables.
        cv.Optional(CONF_ARBITRATION_DELAY, default=DEFAULT_ARBITRATION_DELAY_US): cv.int_range(
            min=0, max=620
        ),
        # Optional RGB light entity driven by the hub as a status LED:
        # green = bus traffic seen recently, red = bus silent (not connected
        # or stuck), blue = listen-only mode.
        cv.Optional(CONF_STATUS_LED): cv.use_id(light.LightState),
    }
).extend(uart.UART_DEVICE_SCHEMA).extend(cv.COMPONENT_SCHEMA)

# eBUS on the wire is 2400 baud, 8 data bits, even parity, 1 stop bit. The
# eBUS Adapter Shield front-end regenerates the signal without the parity bit
# (10-bit frames), so the UART must run 8N1 to match it.
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "ebus",
    baud_rate=2400,
    require_tx=True,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_address(config[CONF_EBUS_ADDRESS]))
    cg.add(var.set_listen_only(config[CONF_LISTEN_ONLY]))
    cg.add(var.set_arbitration_delay(config[CONF_ARBITRATION_DELAY]))
    if CONF_STATUS_LED in config:
        status_led = await cg.get_variable(config[CONF_STATUS_LED])
        cg.add(var.set_status_led(status_led))

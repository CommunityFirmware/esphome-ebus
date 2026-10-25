# Vaillant VRC720 register catalog — single source of truth.
#
# Every register exposed by the vaillant_vrc720 component is defined here.
# The Python platform schemas validate user YAML against this catalog, and the
# codegen passes the protocol coordinates (op/gg/rr/instance/value type) to the
# C++ entity classes.
#
# Sources: https://github.com/Project-Helianthus/helianthus-docs-ebus
#   protocols/vaillant/ebus-vaillant-B524.md
#   protocols/vaillant/ebus-vaillant-B524-register-map.md
#   protocols/vaillant/ebus-vaillant-regulators.md
#
# Writes follow the documented B524 write templates. They are marked
# "candidate, unconfirmed on the wire" in the spec — see README.md.

# ---- value types (must match B524ValueType in vaillant_b524.h) -------------
F32 = "f32"
U16 = "u16"
U8 = "u8"
I8 = "i8"
U32 = "u32"
BOOL = "bool"
STRING = "string"
ENUM16 = "enum16"
DATE = "date"
TIME = "time"
VERSION = "version"
RAW = "raw"

VALUE_TYPES = (F32, U16, U8, I8, U32, BOOL, STRING, ENUM16, DATE, TIME, VERSION, RAW)

# ---- entity kinds -----------------------------------------------------------
SENSOR = "sensor"
BINARY_SENSOR = "binary_sensor"
NUMBER = "number"
SELECT = "select"
TEXT_SENSOR = "text_sensor"
BUTTON = "button"

ENTITY_KINDS = (SENSOR, BINARY_SENSOR, NUMBER, SELECT, TEXT_SENSOR, BUTTON)

# ---- instance kinds ---------------------------------------------------------
INSTANCE_NONE = "none"
INSTANCE_CIRCUIT = "circuit"  # OP=0x02 GG=0x02, II 0..9
INSTANCE_ZONE = "zone"        # OP=0x02 GG=0x03, II 0..0x0A
INSTANCE_DEVICE = "device"    # OP=0x06 GG=0x09/0x0A/0x0C, II 1..8 (one-based)

INSTANCE_KINDS = (INSTANCE_NONE, INSTANCE_CIRCUIT, INSTANCE_ZONE, INSTANCE_DEVICE)

INSTANCE_RANGES = {
    INSTANCE_CIRCUIT: (0, 9),
    INSTANCE_ZONE: (0, 0x0A),
    INSTANCE_DEVICE: (1, 8),
}

# YAML key used to select the instance for each instance kind.
INSTANCE_KEYS = {
    INSTANCE_CIRCUIT: "circuit",
    INSTANCE_ZONE: "zone",
    INSTANCE_DEVICE: "device",
}

# ---- opcodes / groups -------------------------------------------------------
OP_SYSTEM_INFO = 0x00  # ReadSystemInformation: request "00 IDlo IDhi", reply raw f32 LE
OP_LOCAL = 0x02        # GetParameter / SetParameter (local selector family)
OP_REMOTE = 0x06       # GetDeviceParameter / SetDeviceParameter (controller-mediated)

GG_SYSTEM = 0x00       # OP=0x02: system
GG_DHW = 0x01          # OP=0x02: native domestic hot water
GG_CIRCUITS = 0x02     # OP=0x02: heating circuits
GG_ZONES = 0x03        # OP=0x02: zones
GG_REGULATORS = 0x09   # OP=0x06: remote control regulators (VRC7xx, VRT38x)
GG_THERMOSTATS = 0x0A  # OP=0x06: remote control thermostats (VR9x)
GG_VR71 = 0x0C         # OP=0x06: functional modules (VR71) FM5

# ---- write encodings --------------------------------------------------------
WRITE_F32 = "f32"  # 4 bytes IEEE754 little-endian
WRITE_U16 = "u16"  # 2 bytes little-endian (also enum16: low byte + 0x00)
WRITE_U8 = "u8"    # 1 byte

# ---- enums ------------------------------------------------------------------
# All labels use the VRC720-family terms from helianthus-docs-ebus (register
# map "Enumerations that differ by family", requester matrix VRC720 column,
# VRC720 operating & installation manual); ebusd/community and VRC700-family
# labels are deliberately not used.
#
# opmode (GG=0x01 RR=0x0003, GG=0x03 RR=0x0006): VRC720 family labels are
# 0=Off, 1=TimeControlled, 2=Manual (register map + requester matrix). The
# VRC700-family labels (1=Auto, 2=Day, 3=Setback) and value 3 do not exist on
# the VRC720 family.
OPMODE_ZONE_OPTIONS = ["off", "time_controlled", "manual"]
OPMODE_DHW_OPTIONS = ["off", "time_controlled", "manual"]
# zone_special_function_mode (GG=0x03 RR=0x000E): the VRC720 family supports
# only the functions listed under zone_special_function_state (RR=0x001B:
# 0=NoSpecialFunctionActive, 1=VentilationBoost, 4=QuickVeto, 7=Holiday,
# 10=SystemOff), and of those only 0 and 1 are written through RR=0x000E
# (write sequences: quick veto is set via RR=0x0008/0x0026 and cancelled with
# RR=0x000E=00, holiday via the system sequence, system off via GG=0x00
# RR=0x0007). Values 2..5 of the generic sfmode enum have no VRC720 write
# basis and are not offered.
SFMODE_ZONE_OPTIONS = ["no_special_function_active", "ventilation_boost"]
SFMODE_ZONE_VALUES = [0, 1]
# dhw_special_function_mode (GG=0x01 RR=0x000D): write template values are
# 0=cancel and 6=start a one-time DHW load. Labels follow the VRC720-family
# dhw_special_function_state (RR=0x000F) terms: 0=NoSpecialFunctionActive,
# 9=OneTimeTankloading (the state the one-time load leads to).
SFMODE_DHW_OPTIONS = ["no_special_function_active", "one_time_tank_loading"]
SFMODE_DHW_VALUES = [0, 6]
# mctype (GG=0x02 RR=0x0002): Vaillant VRC720 manual names. Value 4 is the
# manual's "Increase in return"; pool=5 is VRC700-only and not offered.
MCTYPE_OPTIONS = ["inactive", "heating", "fixed_value", "dhw", "increase_in_return"]
# offmode (GG=0x02 RR=0x000E): VRC720-family labels 0=Eco, 1=SetBack
# (requester matrix); "night" is the ebusd label.
OFFMODE_OPTIONS = ["eco", "set_back"]
# rcmode (GG=0x02 RR=0x0015): register-map Enum Reference labels; no
# family-specific VRC720 label set is documented beyond the candidate matrix.
RCMODE_OPTIONS = ["off", "modulating", "thermostat"]
# zmapping (GG=0x03 RR=0x0013): VRC720-family binding labels 0=NONE,
# 1=CENTRAL_CONTROL, 2..=REMOTE_CONTROL1.. (protocol doc 4.10 + requester
# matrix). The ebusd hardware names (VRC700, VR91_1..3) and the aliases
# "regulator"/"thermostat_*" are not used. Range stays 0..4 per the
# register-map Enum Reference; the VRC720-family candidate labels continue
# with REMOTE_CONTROL4 at value 5 (revisit if a live device reports it).
ZMAP_OPTIONS = ["none", "central_control", "remote_control_1", "remote_control_2", "remote_control_3"]
CIRCUIT_RCM_OPTIONS = ["inactive", "active", "extended"]

# Entity categories
ENTITY_CATEGORY_CONFIG = "config"
ENTITY_CATEGORY_DIAGNOSTIC = "diagnostic"

# Icons
ICON_TEMPERATURE = "mdi:thermometer"
ICON_WATER = "mdi:water-thermometer"
ICON_FIRE = "mdi:fire"
ICON_SNOWFLAKE = "mdi:snowflake"
ICON_PRESSURE = "mdi:gauge"
ICON_ENERGY = "mdi:lightning-bolt"
ICON_COUNTER = "mdi:counter"
ICON_CLOCK = "mdi:clock-outline"
ICON_CALENDAR = "mdi:calendar"
ICON_HUMIDITY = "mdi:water-percent"
ICON_VALVE = "mdi:valve"
ICON_PUMP = "mdi:pump"
ICON_SIGNAL = "mdi:signal"
ICON_ALERT = "mdi:alert-circle-outline"
ICON_FACTORY = "mdi:factory"
ICON_BOILER = "mdi:boiler"
ICON_RADIATOR = "mdi:radiator"
ICON_HOME = "mdi:home-thermometer"
ICON_VENTILATION = "mdi:air-filter"
ICON_POWER = "mdi:power-plug"
ICON_REFRESH = "mdi:refresh"
ICON_CANCEL = "mdi:cancel"
ICON_PLAY = "mdi:play-circle-outline"
ICON_COG = "mdi:cog"
ICON_INFORMATION = "mdi:information-outline"
ICON_CHIP = "mdi:chip"
ICON_SUN_THERMOMETER = "mdi:sun-thermometer"
ICON_CHART_BELL_CURVE_CUMULATIVE = "mdi:chart-bell-curve-cumulative"
ICON_THERMOMETER_PLUS = "mdi:thermometer-plus"
ICON_WRENCH_CLOCK = "mdi:wrench-clock"
ICON_DEVICES = "mdi:devices"
ICON_ROOM = "mdi:floor-plan"


def _entry(name, *, op, gg, rr, value_type, entity_kind, instance_kind=INSTANCE_NONE,
           unit=None, device_class=None, state_class=None, accuracy_decimals=None,
           icon=None, entity_category=None, inverted=None,
           writable=False, write_encoding=None, write_instance=None,
           min_value=None, max_value=None, step=None,
           options=None, option_values=None,
           button_value=None):
    entry = {
        "name": name,
        "op": op,
        "gg": gg,
        "rr": rr,
        "value_type": value_type,
        "entity_kind": entity_kind,
        "instance_kind": instance_kind,
        "unit": unit,
        "device_class": device_class,
        "state_class": state_class,
        "accuracy_decimals": accuracy_decimals,
        "icon": icon,
        "entity_category": entity_category,
        "inverted": inverted,
        "writable": writable,
        "write_encoding": write_encoding,
        "write_instance": write_instance,
        "min_value": min_value,
        "max_value": max_value,
        "step": step,
        "options": options,
        "option_values": option_values,
        "button_value": button_value,
    }
    if instance_kind != INSTANCE_NONE:
        entry["instance_min"], entry["instance_max"] = INSTANCE_RANGES[instance_kind]
    return entry


def _sensor(name, **kw):
    kw.setdefault("entity_kind", SENSOR)
    return _entry(name, **kw)


def _binary(name, **kw):
    kw.setdefault("entity_kind", BINARY_SENSOR)
    kw.setdefault("value_type", BOOL)
    return _entry(name, **kw)


def _number(name, *, write_encoding, write_instance, min_value, max_value, step, **kw):
    kw.setdefault("entity_kind", NUMBER)
    return _entry(name, writable=True, write_encoding=write_encoding, write_instance=write_instance,
                  min_value=min_value, max_value=max_value, step=step, **kw)


def _select(name, *, write_instance, options, option_values=None, **kw):
    kw.setdefault("entity_kind", SELECT)
    return _entry(name, writable=True, write_encoding=WRITE_U16 if kw.get("value_type", ENUM16) == ENUM16 else WRITE_U8,
                  write_instance=write_instance, options=options, option_values=option_values, **kw)


def _button(name, *, write_instance, value, **kw):
    kw.setdefault("entity_kind", BUTTON)
    return _entry(name, writable=True, write_encoding=WRITE_U8, write_instance=write_instance,
                  button_value=[value], **kw)


def _text(name, **kw):
    kw.setdefault("entity_kind", TEXT_SENSOR)
    return _entry(name, **kw)


# Temperature sensor defaults
_TEMP = dict(unit="°C", device_class="temperature", state_class="measurement", accuracy_decimals=1)
_TEMP_DELTA = dict(unit="K", device_class="temperature_delta", state_class="measurement", accuracy_decimals=1)
_HUMIDITY = dict(unit="%", device_class="humidity", state_class="measurement", accuracy_decimals=1)
_ENERGY = dict(unit="kWh", device_class="energy", state_class="total_increasing", accuracy_decimals=0)
_DURATION = dict(unit="h", device_class="duration", state_class="total_increasing", accuracy_decimals=0)

_REGISTERS = []

# ============================ OP=0x00 system information ====================
_REGISTERS += [
    _sensor("circuit_count_maximum", op=OP_SYSTEM_INFO, gg=0x00, rr=0x0000, value_type=F32,
            state_class="measurement", accuracy_decimals=0, icon=ICON_RADIATOR,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _sensor("zone_count_maximum", op=OP_SYSTEM_INFO, gg=0x00, rr=0x0001, value_type=F32,
            state_class="measurement", accuracy_decimals=0, icon=ICON_ROOM,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _sensor("interface_version", op=OP_SYSTEM_INFO, gg=0x00, rr=0x0006, value_type=F32,
            state_class="measurement", accuracy_decimals=0, icon=ICON_INFORMATION,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _sensor("interface_revision", op=OP_SYSTEM_INFO, gg=0x00, rr=0x0007, value_type=F32,
            state_class="measurement", accuracy_decimals=0, icon=ICON_INFORMATION,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
]

# ============================ OP=0x02 GG=0x00 system =========================
_REGISTERS += [
    # --- sensors ---
    _sensor("outdoor_temperature", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0073, value_type=F32,
            icon=ICON_SUN_THERMOMETER, **_TEMP),
    _sensor("outdoor_temperature_daily_average", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0095, value_type=F32,
            icon=ICON_SUN_THERMOMETER, **_TEMP),
    _sensor("flow_temperature", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x004B, value_type=F32,
            icon=ICON_TEMPERATURE, **_TEMP),
    _sensor("water_pressure", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0039, value_type=F32,
            unit="bar", device_class="pressure", state_class="measurement", accuracy_decimals=2,
            icon=ICON_PRESSURE),
    _sensor("hydraulic_scheme_code", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0036, value_type=U16,
            state_class="measurement", accuracy_decimals=0, icon=ICON_FACTORY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _sensor("energy_manager_state", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0048, value_type=U16,
            state_class="measurement", accuracy_decimals=0, icon=ICON_FIRE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _sensor("dhw_cylinder_top_temperature", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x009D, value_type=F32,
            icon=ICON_WATER, **_TEMP),
    _sensor("dhw_cylinder_bottom_temperature", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x009E, value_type=F32,
            icon=ICON_WATER, **_TEMP),
    _sensor("heating_cylinder_top_temperature", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x009F, value_type=F32,
            icon=ICON_WATER, **_TEMP),
    _sensor("heating_cylinder_bottom_temperature", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00A0, value_type=F32,
            icon=ICON_WATER, **_TEMP),
    _sensor("priority_error_code", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00F0, value_type=U16,
            state_class="measurement", accuracy_decimals=0, icon=ICON_ALERT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _sensor("priority_maintenance_code", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00F1, value_type=U16,
            state_class="measurement", accuracy_decimals=0, icon=ICON_ALERT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    # --- text sensors ---
    _text("date", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0034, value_type=DATE, icon=ICON_CALENDAR,
          entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _text("clock_time", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0035, value_type=TIME, icon=ICON_CLOCK,
          entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    # --- binary sensors ---
    _binary("maintenance_active", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0096, value_type=U8,
            device_class="problem", icon=ICON_WRENCH_CLOCK, entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _binary("outdoor_temperature_sensor_ok", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00B5, value_type=U8,
            device_class="problem", inverted=True, icon=ICON_SUN_THERMOMETER,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    # --- energy counters ---
    _sensor("heating_fuel_consumption_current_month", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x004E, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("dhw_fuel_consumption_current_month", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0051, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("heating_fuel_consumption_previous_month", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0052, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("dhw_fuel_consumption_previous_month", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0055, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("heating_fuel_consumption_total", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0056, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("dhw_fuel_consumption_total", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0059, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("heating_electricity_consumption_current_month", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x004F, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("dhw_electricity_consumption_current_month", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0050, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("heating_electricity_consumption_previous_month", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0053, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("dhw_electricity_consumption_previous_month", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0054, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("heating_electricity_consumption_total", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0057, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("dhw_electricity_consumption_total", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0058, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    _sensor("electricity_consumption_1_total", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x005C, value_type=U32,
            icon=ICON_ENERGY, **_ENERGY),
    # --- numbers (writable) ---
    _number("continuous_heating_outdoor_temperature_limit", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0002,
            value_type=F32, write_encoding=WRITE_F32, write_instance=0x00,
            min_value=-26, max_value=10, step=1, icon=ICON_SNOWFLAKE,
            entity_category=ENTITY_CATEGORY_CONFIG, **_TEMP),
    _number("frost_protection_delay_time", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0003, value_type=U16,
            write_encoding=WRITE_U16, write_instance=0x00,
            min_value=0, max_value=12, step=1, unit="h", icon=ICON_SNOWFLAKE,
            state_class="measurement", accuracy_decimals=0),
    _number("adaptive_heating_curve_enabled", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0014, value_type=U8,
            write_encoding=WRITE_U8, write_instance=0x00,
            min_value=0, max_value=1, step=1, icon=ICON_COG),
    _number("heating_outdoor_alternative_temperature_limit", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0022,
            value_type=F32, write_encoding=WRITE_F32, write_instance=0x00,
            min_value=-21, max_value=40, step=1, icon=ICON_TEMPERATURE, **_TEMP),
    _number("heating_outdoor_bivalence_temperature_limit", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0023,
            value_type=F32, write_encoding=WRITE_F32, write_instance=0x00,
            min_value=-20, max_value=30, step=1, icon=ICON_FIRE, **_TEMP),
    _number("heating_flow_temperature_setpoint_maximum_at_heat_pump_fault", op=OP_LOCAL, gg=GG_SYSTEM,
            rr=0x0026, value_type=F32, write_encoding=WRITE_F32, write_instance=0x00,
            min_value=20, max_value=80, step=1, icon=ICON_BOILER, **_TEMP),
    _number("cylinder_loading_start_hysteresis", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0027, value_type=F32,
            write_encoding=WRITE_F32, write_instance=0xFF,
            min_value=3, max_value=20, step=0.5, icon=ICON_WATER,
            entity_category=ENTITY_CATEGORY_CONFIG, **_TEMP_DELTA),
    _number("cylinder_loading_offset", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0029, value_type=F32,
            write_encoding=WRITE_F32, write_instance=0x00,
            min_value=0, max_value=40, step=1, icon=ICON_THERMOMETER_PLUS,
            entity_category=ENTITY_CATEGORY_CONFIG, **_TEMP_DELTA),
    _number("outdoor_temperature_offset", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x002D, value_type=F32,
            write_encoding=WRITE_F32, write_instance=0x00,
            min_value=-3, max_value=3, step=0.5, icon=ICON_TEMPERATURE,
            entity_category=ENTITY_CATEGORY_CONFIG, **_TEMP_DELTA),
    _number("cooling_start_temperature_limit", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0038, value_type=F32,
            write_encoding=WRITE_F32, write_instance=0x00,
            min_value=10, max_value=30, step=1, icon=ICON_SNOWFLAKE, **_TEMP),
    _number("dhw_flow_temperature_setpoint_maximum", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0046, value_type=F32,
            write_encoding=WRITE_F32, write_instance=0x00,
            min_value=15, max_value=80, step=1, icon=ICON_WATER, **_TEMP),
    _number("continuous_heating_room_temperature_setpoint", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00EF,
            value_type=F32, write_encoding=WRITE_F32, write_instance=0x00,
            min_value=5, max_value=30, step=0.5, icon=ICON_HOME, **_TEMP),
    _number("room_temperature_setpoint_at_holiday", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00F6, value_type=F32,
            write_encoding=WRITE_F32, write_instance=0x00,
            min_value=5, max_value=30, step=0.5, icon=ICON_HOME, **_TEMP),
    _number("external_energy_management_enabled", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00B6, value_type=U8,
            write_encoding=WRITE_U8, write_instance=0xFF,
            min_value=0, max_value=1, step=1, icon=ICON_POWER),
    _number("ventilation_boost_active", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00F9, value_type=U8,
            write_encoding=WRITE_U8, write_instance=0x00,
            min_value=0, max_value=1, step=1, icon=ICON_VENTILATION),
    # --- buttons (commands) ---
    _button("holiday_abort", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00F7, value_type=U8,
            write_instance=0x00, value=1, icon=ICON_CANCEL),
    _button("cooling_abort", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x00F8, value_type=U8,
            write_instance=0x00, value=1, icon=ICON_CANCEL),
    _button("settings_reset_command", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x004A, value_type=U8,
            write_instance=0x00, value=1, icon=ICON_REFRESH, entity_category=ENTITY_CATEGORY_CONFIG),
    _button("boiler_reset_command", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x0098, value_type=U8,
            write_instance=0x00, value=1, icon=ICON_REFRESH, entity_category=ENTITY_CATEGORY_CONFIG),
    _button("time_program_reset_command", op=OP_LOCAL, gg=GG_SYSTEM, rr=0x009B, value_type=U8,
            write_instance=0x00, value=1, icon=ICON_REFRESH, entity_category=ENTITY_CATEGORY_CONFIG),
]

# ============================ OP=0x02 GG=0x01 DHW ============================
_REGISTERS += [
    _binary("dhw_circuit_type", op=OP_LOCAL, gg=GG_DHW, rr=0x0001, value_type=U16,
            icon=ICON_WATER),
    _binary("dhw_circulation_pump_active", op=OP_LOCAL, gg=GG_DHW, rr=0x0002, value_type=U8,
            device_class="running", icon=ICON_PUMP),
    _binary("dhw_pump_active", op=OP_LOCAL, gg=GG_DHW, rr=0x0006, value_type=U8,
            device_class="running", icon=ICON_PUMP),
    _binary("dhw_central_control_active", op=OP_LOCAL, gg=GG_DHW, rr=0x000E, value_type=U8,
            icon=ICON_WATER),
    _sensor("dhw_temperature", op=OP_LOCAL, gg=GG_DHW, rr=0x0005, value_type=F32,
            icon=ICON_WATER, **_TEMP),
    _sensor("dhw_flow_temperature_setpoint", op=OP_LOCAL, gg=GG_DHW, rr=0x0008, value_type=F32,
            icon=ICON_WATER, **_TEMP),
    _sensor("dhw_special_function_state", op=OP_LOCAL, gg=GG_DHW, rr=0x000F, value_type=U16,
            state_class="measurement", accuracy_decimals=0, icon=ICON_INFORMATION,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    _number("dhw_temperature_setpoint", op=OP_LOCAL, gg=GG_DHW, rr=0x0004, value_type=F32,
            write_encoding=WRITE_F32, write_instance=0xFF,
            min_value=35, max_value=70, step=1, icon=ICON_WATER, **_TEMP),
    _select("dhw_operating_mode", op=OP_LOCAL, gg=GG_DHW, rr=0x0003, value_type=ENUM16,
            write_instance=0xFF, options=OPMODE_DHW_OPTIONS, icon=ICON_WATER),
    _select("dhw_special_function_mode", op=OP_LOCAL, gg=GG_DHW, rr=0x000D, value_type=U8,
            write_instance=0xFF, options=SFMODE_DHW_OPTIONS, option_values=SFMODE_DHW_VALUES,
            icon=ICON_PLAY),
    _button("one_time_dhw_load", op=OP_LOCAL, gg=GG_DHW, rr=0x000D, value_type=U8,
            write_instance=0xFF, value=6, icon=ICON_PLAY),
    _button("dhw_special_function_cancel", op=OP_LOCAL, gg=GG_DHW, rr=0x000D, value_type=U8,
            write_instance=0xFF, value=0, icon=ICON_CANCEL),
]

# ============================ OP=0x02 GG=0x02 circuits =======================
_CIRCUIT_SENSOR = dict(instance_kind=INSTANCE_CIRCUIT)
_REGISTERS += [
    _select("circuit_type", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0002, value_type=ENUM16,
            write_instance=None, options=MCTYPE_OPTIONS, icon=ICON_RADIATOR, **_CIRCUIT_SENSOR),
    _select("circuit_room_influence_mode", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0003, value_type=U8,
            write_instance=None, options=CIRCUIT_RCM_OPTIONS, icon=ICON_HOME, **_CIRCUIT_SENSOR),
    _number("circuit_cooling_enabled", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0006, value_type=U8,
            write_encoding=WRITE_U8, write_instance=None,
            min_value=0, max_value=1, step=1, icon=ICON_SNOWFLAKE, **_CIRCUIT_SENSOR),
    _sensor("circuit_flow_temperature_setpoint", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0007, value_type=F32,
            icon=ICON_TEMPERATURE, **_TEMP, **_CIRCUIT_SENSOR),
    _sensor("circuit_flow_temperature", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0008, value_type=F32,
            icon=ICON_TEMPERATURE, **_TEMP, **_CIRCUIT_SENSOR),
    _sensor("heating_circuit_flow_temperature_offset", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x000B,
            value_type=F32, icon=ICON_TEMPERATURE, **_TEMP_DELTA, **_CIRCUIT_SENSOR),
    _select("circuit_frost_protection_mode", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x000E, value_type=ENUM16,
            write_instance=None, options=OFFMODE_OPTIONS, icon=ICON_SNOWFLAKE, **_CIRCUIT_SENSOR),
    _number("circuit_heating_curve_factor", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x000F, value_type=F32,
            write_encoding=WRITE_F32, write_instance=None,
            min_value=0.2, max_value=4.0, step=0.1, icon=ICON_CHART_BELL_CURVE_CUMULATIVE,
            entity_category=ENTITY_CATEGORY_CONFIG, **_CIRCUIT_SENSOR),
    _number("heating_circuit_flow_temperature_setpoint_maximum", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0010,
            value_type=F32, write_encoding=WRITE_F32, write_instance=None,
            min_value=15, max_value=80, step=1, icon=ICON_TEMPERATURE, **_TEMP, **_CIRCUIT_SENSOR),
    _number("circuit_flow_temperature_setpoint_minimum", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0012,
            value_type=F32, write_encoding=WRITE_F32, write_instance=None,
            min_value=15, max_value=80, step=1, icon=ICON_TEMPERATURE, **_TEMP, **_CIRCUIT_SENSOR),
    _number("heating_circuit_outdoor_temperature_limit_maximum", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0014,
            value_type=F32, write_encoding=WRITE_F32, write_instance=None,
            min_value=10, max_value=30, step=1, icon=ICON_SNOWFLAKE, **_TEMP, **_CIRCUIT_SENSOR),
    _select("circuit_room_temperature_influence_mode", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0015,
            value_type=ENUM16, write_instance=None, options=RCMODE_OPTIONS, icon=ICON_HOME,
            **_CIRCUIT_SENSOR),
    _sensor("circuit_mixer_movement", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x001A, value_type=F32,
            unit="%", state_class="measurement", accuracy_decimals=0, icon=ICON_VALVE, **_CIRCUIT_SENSOR),
    _sensor("circuit_state", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x001E, value_type=U16,
            state_class="measurement", accuracy_decimals=0, icon=ICON_FIRE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_CIRCUIT_SENSOR),
    _sensor("circuit_mixer_position", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0021, value_type=F32,
            unit="%", state_class="measurement", accuracy_decimals=0, icon=ICON_VALVE, **_CIRCUIT_SENSOR),
    _sensor("circuit_room_humidity", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0022, value_type=F32,
            icon=ICON_HUMIDITY, **_HUMIDITY, **_CIRCUIT_SENSOR),
    _sensor("circuit_dew_point_temperature", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0023, value_type=F32,
            icon=ICON_TEMPERATURE, **_TEMP, **_CIRCUIT_SENSOR),
    _sensor("circuit_pump_operating_time_total", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0024, value_type=U32,
            icon=ICON_PUMP, **_DURATION, **_CIRCUIT_SENSOR),
    _sensor("circuit_pump_start_count_total", op=OP_LOCAL, gg=GG_CIRCUITS, rr=0x0025, value_type=U32,
            unit="starts", state_class="total_increasing", accuracy_decimals=0, icon=ICON_COUNTER,
            **_CIRCUIT_SENSOR),
]

# ============================ OP=0x02 GG=0x03 zones ==========================
_ZONE_SENSOR = dict(instance_kind=INSTANCE_ZONE)
_REGISTERS += [
    _select("heating_zone_operating_mode", op=OP_LOCAL, gg=GG_ZONES, rr=0x0006, value_type=ENUM16,
            write_instance=None, options=OPMODE_ZONE_OPTIONS, icon=ICON_HOME, **_ZONE_SENSOR),
    _number("zone_room_temperature_setpoint_at_quick_veto", op=OP_LOCAL, gg=GG_ZONES, rr=0x0008,
            value_type=F32, write_encoding=WRITE_F32, write_instance=None,
            min_value=5, max_value=30, step=0.5, icon=ICON_HOME, **_TEMP, **_ZONE_SENSOR),
    _number("heating_zone_room_setback_temperature_setpoint", op=OP_LOCAL, gg=GG_ZONES, rr=0x0009,
            value_type=F32, write_encoding=WRITE_F32, write_instance=None,
            min_value=5, max_value=30, step=0.5, icon=ICON_HOME, **_TEMP, **_ZONE_SENSOR),
    _select("zone_special_function_mode", op=OP_LOCAL, gg=GG_ZONES, rr=0x000E, value_type=U8,
            write_instance=None, options=SFMODE_ZONE_OPTIONS, option_values=SFMODE_ZONE_VALUES,
            icon=ICON_PLAY, **_ZONE_SENSOR),
    _sensor("zone_room_temperature", op=OP_LOCAL, gg=GG_ZONES, rr=0x000F, value_type=F32,
            icon=ICON_TEMPERATURE, **_TEMP, **_ZONE_SENSOR),
    _number("zone_enabled", op=OP_LOCAL, gg=GG_ZONES, rr=0x0011, value_type=U16,
            write_encoding=WRITE_U8, write_instance=None,
            min_value=0, max_value=1, step=1, icon=ICON_HOME, **_ZONE_SENSOR),
    _binary("zone_valve_position", op=OP_LOCAL, gg=GG_ZONES, rr=0x0012, value_type=U16,
            icon=ICON_VALVE, **_ZONE_SENSOR),
    _select("zone_room_sensor_binding_mode", op=OP_LOCAL, gg=GG_ZONES, rr=0x0013, value_type=ENUM16,
            write_instance=None, options=ZMAP_OPTIONS, icon=ICON_SIGNAL, **_ZONE_SENSOR),
    _sensor("heating_zone_room_effective_temperature_setpoint", op=OP_LOCAL, gg=GG_ZONES, rr=0x0014,
            value_type=F32, icon=ICON_TEMPERATURE, **_TEMP, **_ZONE_SENSOR),
    _text("zone_name", op=OP_LOCAL, gg=GG_ZONES, rr=0x0016, value_type=STRING, icon=ICON_ROOM,
          entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_ZONE_SENSOR),
    _sensor("zone_special_function_state", op=OP_LOCAL, gg=GG_ZONES, rr=0x001B, value_type=U16,
            state_class="measurement", accuracy_decimals=0, icon=ICON_INFORMATION,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_ZONE_SENSOR),
    _sensor("zone_circuit_assignment", op=OP_LOCAL, gg=GG_ZONES, rr=0x001C, value_type=I8,
            state_class="measurement", accuracy_decimals=0, icon=ICON_RADIATOR,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_ZONE_SENSOR),
    _binary("heating_zone_time_program_active", op=OP_LOCAL, gg=GG_ZONES, rr=0x0019, value_type=U16,
            icon=ICON_CLOCK, **_ZONE_SENSOR),
    _number("heating_zone_room_manual_temperature_setpoint", op=OP_LOCAL, gg=GG_ZONES, rr=0x0022,
            value_type=F32, write_encoding=WRITE_F32, write_instance=None,
            min_value=15, max_value=30, step=0.5, icon=ICON_HOME, **_TEMP, **_ZONE_SENSOR),
    _number("zone_quick_veto_time", op=OP_LOCAL, gg=GG_ZONES, rr=0x0026, value_type=F32,
            write_encoding=WRITE_F32, write_instance=None,
            min_value=0.5, max_value=12, step=0.5, unit="h", icon=ICON_CLOCK,
            state_class="measurement", accuracy_decimals=1, **_ZONE_SENSOR),
    _sensor("zone_room_humidity", op=OP_LOCAL, gg=GG_ZONES, rr=0x0028, value_type=F32,
            icon=ICON_HUMIDITY, **_HUMIDITY, **_ZONE_SENSOR),
    _binary("zone_cooling_enabled", op=OP_LOCAL, gg=GG_ZONES, rr=0x0029, value_type=U16,
            icon=ICON_SNOWFLAKE, **_ZONE_SENSOR),
    _binary("zone_summer_cutoff_active", op=OP_LOCAL, gg=GG_ZONES, rr=0x002A, value_type=U16,
            icon=ICON_SNOWFLAKE, **_ZONE_SENSOR),
    _binary("zone_continuous_heating_active", op=OP_LOCAL, gg=GG_ZONES, rr=0x002B, value_type=U16,
            icon=ICON_FIRE, **_ZONE_SENSOR),
    _binary("zone_frost_protection_active", op=OP_LOCAL, gg=GG_ZONES, rr=0x002C, value_type=U16,
            icon=ICON_SNOWFLAKE, **_ZONE_SENSOR),
    _binary("heating_zone_thermostat_cutoff_active", op=OP_LOCAL, gg=GG_ZONES, rr=0x002D, value_type=U16,
            icon=ICON_HOME, **_ZONE_SENSOR),
    _binary("cooling_zone_thermostat_cutoff_active", op=OP_LOCAL, gg=GG_ZONES, rr=0x002E, value_type=U16,
            icon=ICON_SNOWFLAKE, **_ZONE_SENSOR),
    _button("quick_veto_cancel", op=OP_LOCAL, gg=GG_ZONES, rr=0x000E, value_type=U8,
            write_instance=None, value=0, icon=ICON_CANCEL, **_ZONE_SENSOR),
]

# ============================ OP=0x06 remote devices =========================
# Common OP=0x06 registers (RR 0x0001..0x0004) exist for every device group.
_DEVICE = dict(instance_kind=INSTANCE_DEVICE)


def _device_registers(prefix, gg):
    return [
        _binary(f"{prefix}_device_connected", op=OP_REMOTE, gg=gg, rr=0x0001, value_type=U8,
                device_class="connectivity", icon=ICON_DEVICES,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_DEVICE),
        _sensor(f"{prefix}_device_class_address", op=OP_REMOTE, gg=gg, rr=0x0002, value_type=U8,
                state_class="measurement", accuracy_decimals=0, icon=ICON_INFORMATION,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_DEVICE),
        _sensor(f"{prefix}_device_error_code", op=OP_REMOTE, gg=gg, rr=0x0003, value_type=U8,
                state_class="measurement", accuracy_decimals=0, icon=ICON_ALERT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_DEVICE),
        _text(f"{prefix}_device_firmware_version", op=OP_REMOTE, gg=gg, rr=0x0004, value_type=VERSION,
              icon=ICON_CHIP, entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_DEVICE),
    ]


_REGISTERS += _device_registers("regulator", GG_REGULATORS)
_REGISTERS += _device_registers("thermostat", GG_THERMOSTATS)
_REGISTERS += _device_registers("vr71", GG_VR71)

# Regulator (VRC7xx/VRT38x) and thermostat (VR9x) telemetry.
for _prefix, _gg in (("regulator", GG_REGULATORS), ("thermostat", GG_THERMOSTATS)):
    _REGISTERS += [
        _sensor(f"{_prefix}_room_temperature", op=OP_REMOTE, gg=_gg, rr=0x000F, value_type=F32,
                icon=ICON_TEMPERATURE, **_TEMP, **_DEVICE),
        _sensor(f"{_prefix}_room_humidity", op=OP_REMOTE, gg=_gg, rr=0x0007, value_type=F32,
                icon=ICON_HUMIDITY, **_HUMIDITY, **_DEVICE),
        _sensor(f"{_prefix}_reception_level", op=OP_REMOTE, gg=_gg, rr=0x001F, value_type=U8,
                state_class="measurement", accuracy_decimals=0, icon=ICON_SIGNAL, **_DEVICE),
        _sensor(f"{_prefix}_remote_control_address", op=OP_REMOTE, gg=_gg, rr=0x0019, value_type=U8,
                state_class="measurement", accuracy_decimals=0, icon=ICON_INFORMATION,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_DEVICE),
        _sensor(f"{_prefix}_zone_assignment", op=OP_REMOTE, gg=_gg, rr=0x0025, value_type=U8,
                state_class="measurement", accuracy_decimals=0, icon=ICON_ROOM,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_DEVICE),
        _binary(f"{_prefix}_device_pairing_active", op=OP_REMOTE, gg=_gg, rr=0x001E, value_type=U8,
                icon=ICON_DEVICES, **_DEVICE),
        _sensor(f"{_prefix}_hardware_identifier", op=OP_REMOTE, gg=_gg, rr=0x0023, value_type=U16,
                state_class="measurement", accuracy_decimals=0, icon=ICON_INFORMATION,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC, **_DEVICE),
    ]

REGISTERS = {entry["name"]: entry for entry in _REGISTERS}

# Sanity checks on the catalog itself.
for _e in _REGISTERS:
    assert _e["name"] not in (), "empty name"
    assert _e["value_type"] in VALUE_TYPES, _e["name"]
    assert _e["entity_kind"] in ENTITY_KINDS, _e["name"]
    assert _e["instance_kind"] in INSTANCE_KINDS, _e["name"]
    if _e["entity_kind"] == NUMBER:
        assert _e["writable"] and _e["write_encoding"] and _e["min_value"] is not None, _e["name"]
    if _e["entity_kind"] == SELECT:
        assert _e["writable"] and _e["options"], _e["name"]
        if _e["option_values"] is None:
            _e["option_values"] = list(range(len(_e["options"])))
        assert len(_e["option_values"]) == len(_e["options"]), _e["name"]
    if _e["entity_kind"] == BUTTON:
        assert _e["writable"] and _e["button_value"], _e["name"]
    if _e["writable"]:
        if _e["instance_kind"] == INSTANCE_NONE:
            # Non-instanced registers write to a fixed instance (0x00 or 0xFF).
            if _e["write_instance"] is None:
                _e["write_instance"] = 0
            assert _e["write_instance"] is not None, _e["name"]
        else:
            # Instanced registers resolve the write instance from the YAML
            # instance key (circuit/zone/device) at codegen time.
            assert _e["write_instance"] is None, _e["name"]
    if _e["instance_kind"] == INSTANCE_NONE:
        assert _e["op"] != OP_REMOTE or _e["gg"] in (GG_REGULATORS, GG_THERMOSTATS, GG_VR71), _e["name"]

assert len(REGISTERS) == len(_REGISTERS), "duplicate register names in catalog"

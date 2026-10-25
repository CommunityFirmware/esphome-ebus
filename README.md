<h1 align="center">ESPHome eBUS</h1>

<p align="center">
    Open eBUS implementation to control heating systems
</p>

ESPHome external components that implement the Vaillant eBUS protocol (extensions), _primarly_ to control
and monitor a **VRC 720** system regulator. The target hardware is the
[eBUS Adapter Shield C6](https://adapter.ebusd.eu/v5-c6/index.en) (ESP32-C6),
replacing its closed-source firmware with an ESPHome-based one.

Included components:

| Component | Purpose |
|---|---|
| `ebus` | eBUS transport hub: UART framing, bus arbitration, request queue, passive monitoring, status LED |
| `vaillant_vrc720` | VRC 720 device model: register catalog, discovery, polling, writable entities, climate entities |
| `vaillant_energy` | B516 energy statistics: cumulative kWh counters and current electrical power |

_Requires ESPHome >= 2026.10.0._

## Hardware

The [eBUS Adapter Shield C6](https://adapter.ebusd.eu/v5-c6/index.en.html#io)
hardwires its eBUS transceiver to **GPIO15 (TX)** and **GPIO14 (RX)**. The
shield's front-end inverts the signal once and regenerates it without the
parity bit, so the UART must run **2400 baud, 8N1, with both pins inverted**
(see the quick start below). The `ebus` component validates the UART settings
at config time.

The shield also carries a single addressable RGB LED on **GPIO23** that the
`ebus` hub can drive as a bus status indicator, and supports wired Ethernet
via a USR-ES1 module (W5500).

## Quick start

```yaml
esphome:
  name: vrc720-ebus-adapter

esp32:
  board: esp32-c6-devkitm-1  # ESP32-C6-MINI-1 module, 4 MB flash
  variant: esp32c6
  framework:
    type: esp-idf

external_components:
  - source:
      type: local
      path: esphome/components
    components: [ebus, vaillant_vrc720, vaillant_energy]

logger:
api:
ota:
  - platform: esphome

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

uart:
  - id: ebus_uart
    tx_pin:
      number: GPIO15   # eBUS TX (hardwired on the shield)
      inverted: true   # the shield's front-end inverts the signal
    rx_pin:
      number: GPIO14   # eBUS RX (hardwired on the shield)
      inverted: true
    baud_rate: 2400
    data_bits: 8
    parity: NONE
    stop_bits: 1

ebus:
  id: ebus_hub
  uart_id: ebus_uart
  address: 0x31        # initiator address; both nibbles must be in {0,1,3,7,F}

vaillant_vrc720:
  id: vrc720
  ebus_id: ebus_hub
  address: 0x15        # VRC 720 slave address
  update_interval: 30s

vaillant_energy:
  id: energy
  ebus_id: ebus_hub
  address: 0x15

sensor:
  - platform: vaillant_vrc720
    register: outdoor_temperature
  - platform: vaillant_vrc720
    register: zone_room_temperature
    zone: 0            # heating zone 1 (YAML zones are 0-based)
  - platform: vaillant_energy
    statistics:
      source: electrical
      usage: heating
      period: total    # cumulative kWh, ready for the HA energy dashboard

number:
  - platform: vaillant_vrc720
    register: dhw_temperature_setpoint

climate:
  - platform: vaillant_vrc720
    zone: 0            # room climate for zone 1
  - platform: vaillant_vrc720
    dhw: true          # domestic hot water as a climate entity
```

See `examples/esp32-c6-vrc720.yaml` for a full configuration covering all
entity platforms, the status LED and the Ethernet wiring. The example is a
template: it pulls credentials via `!secret`. Copy it to `config/` (excluded
from Git) and create `config/secrets.yaml` next to it:

```yaml
wifi_ssid: "..."
wifi_password: "..."
```

## Flashing the adapter

The adapter ships with the closed-source eBUS firmware. Replacing it is a
one-time USB flash; all later updates go over the air. See the official
[flashing guide](https://adapter.ebusd.eu/v5-c6/firmware.en.html) for the
hardware procedure — bootloader mode is entered by keeping the adapter's
button pressed while connecting USB.

```bash
# recommended: back up the original firmware first (4 MB)
esptool.py --chip esp32c6 --port /dev/cu.usbmodemXXXX read_flash 0 0x400000 ebus-adapter-backup.bin

# one-time flash over USB (bootloader mode)
esphome upload config/esp32-c6-vrc720.yaml --device /dev/cu.usbmodemXXXX

# all later updates go over the air
esphome upload config/esp32-c6-vrc720.yaml
```

## The `ebus` hub

```yaml
ebus:
  id: ebus_hub
  uart_id: ebus_uart    # required
  address: 0x31         # optional, default 0x31
  listen_only: false    # optional, never transmit, only passively decode
  arbitration_delay: 200  # optional, microseconds to yield after bus-free (0-620)
  status_led: my_status_led  # optional, RGB light entity driven as bus indicator
```

The hub is a `uart` device and a polling-free `Component`. It queues requests
from the device components and runs them as eBUS master transactions:
per-byte echo verification, ACK/NAK handshake with the documented retry rules,
arbitration against competing masters (with lockout and retry), and
CRC-checked response reception. While idle it passively decodes all traffic
from other bus nodes, which the device components use to update entities even
when they are not polling themselves.

- **`listen_only`** — bring-up/debug mode: the hub never transmits. Entities
  still update from traffic that other bus nodes read from the target device.
- **`arbitration_delay`** — microseconds to wait after the bus becomes free
  before starting a telegram. Yielding the start of the bus-free phase lets
  competing initiators win cleanly instead of colliding; 200 µs (the default)
  reduced our retry rate from ~30% to ~3% on a busy bus.
- **`status_led`** — any RGB light entity (the shield's LED on GPIO23 works
  well): green = bus traffic seen recently, red = bus silent, blue =
  listen-only mode.

Wire-level framing, escaping, CRC and all timing constants follow the official
eBUS signal specification; see the
[protocol overview](https://github.com/Project-Helianthus/helianthus-docs-ebus/blob/main/protocols/ebus-services/ebus-overview.md)
for details. Transaction statistics are dumped at boot and available via
getters.

## The `vaillant_vrc720` component

```yaml
vaillant_vrc720:
  id: vrc720
  ebus_id: ebus_hub     # required
  address: 0x15         # optional, default 0x15 (VRC 720)
  update_interval: 30s  # optional, default 30s
```

On setup the component identifies the device, reads the system information
registers and discovers the topology (DHW circuit, heating circuits, zones),
logging what it finds. Each poll cycle it reads every subscribed register once
and dispatches the decoded value to all matching entities. It also decodes
register replies that other bus nodes read from the device, so entities update
from passively observed traffic too.

### Register catalog

All 133 registers are defined in a single source of truth,
`esphome/components/vaillant_vrc720/registers.py`, transcribed from the
unofficial spec. The Python platform schemas validate your YAML against this
catalog, so a typo in `register:` is a config error, not a runtime surprise.

Entities are created per platform with just a register name:

```yaml
sensor:
  - platform: vaillant_vrc720
    register: flow_temperature
```

| Platform | Registers | Notes |
|---|---|---|
| `sensor` | 65 | unit, device class, accuracy, icon, state class defaulted from the catalog |
| `binary_sensor` | 19 | device classes like `problem`, `running`, `connectivity` |
| `text_sensor` | 6 | date, time, version, name strings |
| `number` | 26 | writable setpoints; min/max/step defaulted from the catalog, overridable |
| `select` | 9 | writable operating modes; options defaulted from the catalog |
| `button` | 8 | one-shot commands (e.g. one-time DHW load, holiday abort) |
| `climate` | — | composed entities, see below |

Names are derived from the register (`dhw_temperature_setpoint` →
"DHW Temperature Setpoint"); override with the usual `name:`. Entity
categories (config/diagnostic), icons and device classes are defaulted from
the catalog as well.

Registers that exist once per heating circuit, zone, or remote device take an
instance key: `circuit: 0..9`, `zone: 0..10` (both 0-based) or `device: 1..8`.
The schema cross-validates the register against the instance keys.

### Climate entities

The `climate` platform composes several registers into one Home Assistant
climate entity:

```yaml
climate:
  - platform: vaillant_vrc720
    zone: 0                   # room climate: current/target temperature, humidity,
    quick_veto_duration: 3    # operating mode, quick veto as BOOST preset
  - platform: vaillant_vrc720
    dhw: true                 # DHW: temperatures, mode, one-time load as BOOST preset
```

Zone entities support OFF/AUTO/HEAT modes, target temperature, and the quick
veto (BOOST preset, with a configurable duration in hours). The DHW entity
additionally reports the loading action and maps the one-time tank load to the
BOOST preset. Unsupported presets are not offered.

### Writing to the regulator

`number`, `select`, `button` and `climate` entities send B524 write telegrams.
After a successful write the register is re-read so entities refresh from the
device. The exercised write paths (operating modes, setpoints, one-time DHW
load, quick veto) have been verified against a physical VRC 720 on a live bus;
the remaining write templates follow the documented spec and are marked
candidate there.

## The `vaillant_energy` component

```yaml
vaillant_energy:
  id: energy
  ebus_id: ebus_hub     # required
  address: 0x15         # optional, default 0x15 (the regulator)
  time_id: my_time      # optional, time source for "daily" statistics windows
  update_interval: 60s  # optional, default 60s
```

Polls the Vaillant B516 energy statistics service and publishes the values as
Home Assistant energy sensors:

```yaml
sensor:
  # Cumulative counters (watt-hours on the wire, published in kWh,
  # state_class: total_increasing — ready for the HA energy dashboard).
  - platform: vaillant_energy
    statistics:
      source: electrical     # solar | environmental | electrical | fuel
      usage: heating        # sum | heating | dhw | cooling
      period: total         # total | daily
  # "daily" windows query the packed date of the time source (or an explicit
  # `date:`); they depend on the regulator's own clock being correct.
  - platform: vaillant_energy
    statistics:
      source: electrical
      usage: heating
      period: daily
  # Candidate "current electrical power" sub-ID, queried at a different
  # address (e.g. the heat pump at 0x08); published in W if it answers.
  - platform: vaillant_energy
    power: true
    address: 0x08
```

The `total` counters are all-time values that ignore the date field, so they
stay correct regardless of the regulator's clock. The `daily` windows need the
regulator's own day accounting to match, and the candidate power sub-ID is
unconfirmed on the wire — if the target never answers, the sensor stays
unknown and the query is retried each cycle.

## Development

```
# unit tests (frame codec + B524 codec, host-compiled, no ESPHome needed)
bash tests/unit/run_tests.sh

# config validation (the host test config is self-contained; the device
# template needs a secrets.yaml next to it — see Quick start)
esphome config examples/host-vrc720-test.yaml

# host integration test: runs the compiled host binary against a PTY-based
# eBUS simulator (tests/host_test.py) and asserts telegrams, ACKs, NAK retry,
# writes, and published entity states
esphome compile examples/host-vrc720-test.yaml
python3 tests/host_test.py

# target firmware (config/ holds your device config + secrets.yaml)
esphome compile config/esp32-c6-vrc720.yaml
esphome upload config/esp32-c6-vrc720.yaml
```

## References

- Unofficial eBUS/Vaillant spec (primary reference — wire-level protocol,
  standard services and all Vaillant register definitions):
  <https://github.com/Project-Helianthus/helianthus-docs-ebus>
  (`protocols/ebus-services/`, `protocols/vaillant/`)
- eBUS Adapter Shield C6: <https://adapter.ebusd.eu/v5-c6/index.en>

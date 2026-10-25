#!/usr/bin/env python3
"""Host integration test for the eBUS hub and the vaillant_vrc720 component.

Runs the compiled host-platform firmware (examples/host-vrc720-test.yaml)
against a Python VRC720 simulator connected through a PTY:

- the slave side is symlinked to /tmp/ebus-test-pty, the UART port the test
  config is built with;
- the simulator echoes every byte the hub writes (eBUS adapters echo the
  bus; the hub does a per-byte echo check), ACKs each request telegram and
  answers B524 reads with canned register values;
- the simulator acts as an AUTO-SYN generator: while the bus is free it
  injects a SYN every ~40 ms, so the bus never falls silent for the 51 ms
  idle timeout. The hub must treat a received SYN as "bus free" (and decay
  its arbitration lockout per SYN) or it will never transmit;
- one register read is answered LATE: the response only goes out on the bus
  after the hub's 51 ms ACK timeout has already fired and the hub aborted
  with SYN (slow-target behavior observed on a real VRC720/f3). The hub
  must stay passive until the late answer has drained off the bus
  (EBUS_DRAIN_MS) before starting the next telegram, or the two collide;
- the interval in the test YAML triggers number/select/button writes, which
  the simulator verifies on the wire (OP=0x02 OT=0x01 GG II RR VALUE);
- the firmware log is asserted for the entity states published from the
  polled registers.

Usage: python3 tests/host_test.py [path-to-host-binary]
Exit code 0 = pass, 1 = fail.
"""

import os
import pty
import re
import select
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

DEBUG = bool(os.environ.get("EBUS_TEST_DEBUG"))

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BINARY = (
    REPO_ROOT / "examples/.esphome/build/vrc720-host-test/.pioenvs/vrc720-host-test/program"
)
PTY_LINK = "/tmp/ebus-test-pty"

HUB_ADDR = 0x31  # ebus: address in the test YAML
DEVICE_ADDR = 0x15  # vaillant_vrc720: address in the test YAML

SYN = 0xAA
ESC = 0xA9
ACK = 0x00
NAK = 0xFF
BROADCAST = 0xFE

# ---- CRC8 (poly 0x9B, init 0x00), per the protocol docs / ebus_frame.cpp ----

CRC8_TABLE = []
for _i in range(256):
    _c = _i
    for _ in range(8):
        _c = ((_c << 1) ^ 0x9B) & 0xFF if _c & 0x80 else (_c << 1) & 0xFF
    CRC8_TABLE.append(_c)
assert CRC8_TABLE[0x00] == 0x00 and CRC8_TABLE[0x01] == 0x9B and CRC8_TABLE[0x03] == 0x36


def crc8_update(crc, wire_byte):
    return CRC8_TABLE[crc] ^ wire_byte


def crc8_logical(data):
    """CRC8 over logical bytes, expanding 0xA9/0xAA to their wire form first
    (identical to crc8() in ebus_frame.cpp)."""
    crc = 0
    for b in data:
        if b == ESC:
            crc = crc8_update(crc8_update(crc, ESC), 0x00)
        elif b == SYN:
            crc = crc8_update(crc8_update(crc, ESC), 0x01)
        else:
            crc = crc8_update(crc, b)
    return crc


def escape(data):
    out = bytearray()
    for b in data:
        if b == ESC:
            out += bytes([ESC, 0x00])
        elif b == SYN:
            out += bytes([ESC, 0x01])
        else:
            out.append(b)
    return bytes(out)


def build_response(data):
    """Target response on the wire: escaped LEN DATA... CRC."""
    wire = bytearray(escape(bytes([len(data)]) + bytes(data)))
    crc = 0
    for b in wire:
        crc = crc8_update(crc, b)
    wire += escape(bytes([crc]))
    return bytes(wire)


def is_master_address(addr):
    def nibble_ok(n):
        return n in (0x0, 0x1, 0x3, 0x7, 0xF)

    return nibble_ok((addr >> 4) & 0x0F) and nibble_ok(addr & 0x0F)


# ---- canned VRC720 register values ------------------------------------------

F32 = lambda v: struct.pack("<f", v)
U16 = lambda v: struct.pack("<H", v)
U8 = lambda v: bytes([v])

# (op, gg, rr, ii) -> value bytes, for every register the test YAML polls.
# dhw_temperature = 85.0 makes the reply contain a 0xAA byte, which exercises
# the hub's response unescaping (and the CRC over wire-escaped bytes).
POLL_VALUES = {
    (0x02, 0x00, 0x0073, 0x00): F32(-3.5),  # outdoor_temperature
    (0x02, 0x00, 0x004B, 0x00): F32(42.5),  # flow_temperature
    (0x02, 0x01, 0x0005, 0x00): F32(85.0),  # dhw_temperature
    (0x02, 0x02, 0x0008, 0x00): F32(38.0),  # circuit_flow_temperature (circuit 0)
    (0x02, 0x03, 0x000F, 0x00): F32(21.5),  # zone_room_temperature (zone 0)
    (0x06, 0x09, 0x000F, 0x01): F32(21.0),  # regulator_room_temperature (device 1)
    (0x02, 0x00, 0x0096, 0x00): U8(0),  # maintenance_active
    (0x02, 0x01, 0x0006, 0x00): U8(1),  # dhw_pump_active
    (0x02, 0x03, 0x0012, 0x00): U16(75),  # zone_valve_position (zone 0)
    (0x02, 0x01, 0x0004, 0x00): F32(55.0),  # dhw_temperature_setpoint
    (0x02, 0x03, 0x0022, 0x00): F32(20.0),  # heating_zone_room_manual_temperature_setpoint (zone 0)
    (0x02, 0x01, 0x0003, 0x00): U16(1),  # dhw_operating_mode -> "time_controlled"
    (0x02, 0x03, 0x0006, 0x00): U16(2),  # heating_zone_operating_mode -> "manual"
    (0x02, 0x00, 0x0034, 0x00): bytes([0x22, 0x10, 0x26]),  # date -> 2026-10-22
    (0x02, 0x03, 0x0016, 0x00): b"Living Room\x00",  # zone_name (zone 0)
    (0x06, 0x09, 0x0004, 0x01): bytes([1, 2, 3]),  # regulator_device_firmware_version -> "1.2.3"
    (0x02, 0x03, 0x0028, 0x00): F32(48.5),  # zone_room_humidity (zone climate)
    (0x02, 0x03, 0x001B, 0x00): U16(0),  # zone_special_function_state -> none (zone climate)
    (0x02, 0x01, 0x000F, 0x00): U16(0),  # dhw_special_function_state -> none (dhw climate)
}
# circuit_count_maximum is an OP=0x00 system-info read (ID 0x0000).
SYSINFO_VALUES = {0x0000: 2.0, 0x0001: 2.0, 0x0006: 1.0, 0x0007: 0.0}

# B516 energy statistics (sub-command 0x10): (period, source, usage) -> Wh,
# or None for "no value" (FLAGS return code 1, like the worked evidence's
# electrical-DHW reply). Sub-ID 0x14 replies with a candidate watt value.
B516_VALUES = {
    (0x00, 0x03, 0x03): 15000.0,  # electrical heating total
    (0x00, 0x03, 0x04): None,  # electrical DHW total: no value on this device
    (0x01, 0x03, 0x03): 2500.0,  # electrical heating daily
}
B516_POWER_W = 1250.0

# Identification reply (service 07 04): Vaillant 0xB5, device_id "BASS2",
# sw 12.03 (BCD), SPN 417 (BCD 17 04, LSB first) -> model "VRC720".
IDENTIFICATION = bytes([0xB5]) + b"BASS2" + bytes([0x12, 0x03, 0x17, 0x04])

# Writes the interval in the test YAML must produce:
# (op, gg, ii, rr, value)
EXPECTED_WRITES = [
    (0x02, 0x01, 0xFF, 0x0004, F32(52.0)),  # number.set dhw_setpoint 52.0
    (0x02, 0x01, 0xFF, 0x0003, U16(2)),  # select.set dhw_mode "manual"
    (0x02, 0x01, 0xFF, 0x000D, U8(6)),  # button.press one_time_dhw_load
    # climate.control zone_climate preset "boost": quick veto set point (the
    # polled manual setpoint) then duration (protocol doc 4.9 write order).
    (0x02, 0x03, 0x00, 0x0008, F32(20.0)),  # quick veto setpoint (zone 0)
    (0x02, 0x03, 0x00, 0x0026, F32(3.0)),  # quick veto duration 3h
    # climate.control zone_climate preset "none": cancel via RR=0x000E = 0.
    (0x02, 0x03, 0x00, 0x000E, U8(0)),
    # climate.control dhw_climate preset "boost": one-time load (RR=0x000D = 6).
    (0x02, 0x01, 0xFF, 0x000D, U8(6)),
]

# Log lines the firmware must print (entity states published from polls).
EXPECTED_LOG_LINES = [
    "Identified: manufacturer=0xB5 device_id=BASS2 sw=12.03 SPN=417 model=VRC720",
    "System info 0x0000 = 2.00",
    "System info 0x0001 = 2.00",
    "System info 0x0006 = 1.00",
    "System info 0x0007 = 0.00",
    "DHW circuit present (type=1)",
    "Circuit 1 present (type=1)",
    "Zone 1 present (circuit assignment=1)",
    "'Outdoor Temperature' >> -3.5 °C",
    "'Flow Temperature' >> 42.5 °C",
    "'DHW Temperature' >> 85.0 °C",
    "'Circuit Count Maximum' >> 2",
    "'Circuit Flow Temperature 1' >> 38.0 °C",
    "'Zone Room Temperature 1' >> 21.5 °C",
    "'Regulator Room Temperature 1' >> 21.0 °C",
    "'Maintenance Active' >> OFF",
    "'DHW Pump Active' >> ON",
    "'Zone Valve Position 1' >> ON",
    "'DHW Temperature Setpoint' >> 55.00",
    "'Heating Zone Room Manual Temperature Setpoint 1' >> 20.00",
    "'DHW Operating Mode' >> time_controlled (1)",
    "'Heating Zone Operating Mode 1' >> manual (2)",
    "'Date' >> '2026-10-22'",
    "'Zone Name 1' >> 'Living Room'",
    "'Regulator Device Firmware Version 1' >> '1.2.3'",
    # B516 energy statistics and power sensors.
    "'Electrical Heating Energy Total' >> 15.00 kWh",
    "'Electrical Heating Energy Daily' >> 2.50 kWh",
    "'Current Electrical Power' >> 1250 W",
    # Climate entities publish at verbose level (climate.cpp publish_state).
    "'Zone 1' >>",
    "'DHW' >>",
]

# The DHW topology-discovery read is NAKed once so the hub's NAK-retry path
# (retransmit the whole telegram) is exercised.
NAK_ONCE_REQUEST = bytes([0x02, 0x00, 0x01, 0x00, 0x01, 0x00])  # OP OT GG II RRlo RRhi

# Regular poll register the slow-target scenario answers late (outdoor
# temperature): harmless to lose once, retried on the next poll cycle.
LATE_ANSWER_REQUEST = bytes([0x02, 0x00, 0x00, 0x00, 0x73, 0x00])

# Registers the test YAML writes to (reply flags bit1 = RW candidate).
WRITABLE_KEYS = {
    (0x02, 0x01, 0x0004, 0x00),  # dhw_temperature_setpoint
    (0x02, 0x01, 0x0003, 0x00),  # dhw_operating_mode
    (0x02, 0x01, 0x000D, 0x00),  # one_time_dhw_load
}


class Simulator:
    """Plays a VRC720 regulator on the master side of the PTY."""

    def __init__(self, fd):
        self.fd = fd
        self.state = "COLLECT"  # COLLECT | EXPECT_RESP_ACK | EXPECT_SYN
        self.logical = bytearray()
        self.escape_pending = False
        self.expected_len = None
        self.last_response = b""
        self.nak_once_done = False
        self.collide_once_done = False  # one-time arbitration collision injected
        self.collision_t = None         # when the collision was injected
        self.retry_delay = None         # hub's delay before retrying after the collision
        self.collision_syn_count = None  # AUTO-SYN injections seen up to the collision
        self.retry_syn_count = None       # injections between collision and retry
        self.last_hub_byte_t = None      # last byte received from the hub
        self.last_sim_send_t = None      # last byte written by the simulator
        self.last_syn_inject = None      # last AUTO-SYN injection
        self.syn_inject_count = 0
        self.hub_abort_syns = 0          # hub SYNs that aborted a transaction
        # One-time slow-target scenario: a register read is answered only
        # after the hub's ACK timeout has fired (see EBUS_DRAIN_MS).
        self.late_answer_done = False
        self.late_phase = None          # None | "armed" | "abort_seen" | "done"
        self.late_telegram_end_t = None  # when the slow request completed
        self.late_abort_t = None        # when the hub's abort SYN was seen
        self.late_send_at = None        # when the late answer goes out
        self.late_response = b""
        self.first_tx_after_late = None  # hub's first byte after the abort SYN
        self.poll_counts = {}  # (op, gg, rr, ii) -> answers
        self.sysinfo_counts = {}  # info id -> answers
        self.b516_counts = {}  # (period, source, usage) -> answers
        self.b516_power_count = 0  # sub-ID 0x14 answers
        self.writes = []  # (op, gg, ii, rr, value)
        self.requests = []  # (src, dst, pb, sb, data)
        self.errors = []
        self.recent_rx = bytearray()  # last raw hub bytes (decoder error context)
        self.last_byte_t = None      # last byte received from the hub
        self.stop = False
        self.t0 = time.monotonic()

    def send(self, data):
        """Inject bytes as the simulated regulator (traced in debug mode)."""
        self.last_sim_send_t = time.monotonic()
        os.write(self.fd, bytes(data))
        if DEBUG:
            print(f"[{time.monotonic() - self.t0:8.3f}] SIM->HUB {bytes(data).hex()} state={self.state}",
                  file=sys.stderr)

    # -- byte handling --------------------------------------------------------

    def handle_byte(self, b):
        self.last_hub_byte_t = time.monotonic()
        self.last_byte_t = self.last_hub_byte_t
        self.recent_rx.append(b)
        del self.recent_rx[:-24]
        # One-time arbitration collision: on the hub's very first telegram,
        # echo a competing master address (0x71 — same priority class as the
        # hub's 0x31, low nibble 1) instead of the hub's own address byte. The
        # hub must detect the echo mismatch, apply its arbitration lockout
        # (2 bus-free phases) and only then retry the telegram.
        if (not self.collide_once_done and self.state == "COLLECT" and b == HUB_ADDR
                and not self.logical):
            self.collide_once_done = True
            self.collision_t = time.monotonic()
            self.collision_syn_count = self.syn_inject_count
            os.write(self.fd, bytes([0x71]))
            if DEBUG:
                print(f"[{time.monotonic() - self.t0:8.3f}] HUB->SIM 0x{b:02X} "
                      f"COLLISION->0x71 state={self.state}", file=sys.stderr)
            self.reset_telegram()
            return
        if (self.collision_t is not None and self.retry_delay is None and self.state == "COLLECT"
                and b == HUB_ADDR and not self.logical):
            self.retry_delay = time.monotonic() - self.collision_t
            self.retry_syn_count = self.syn_inject_count - self.collision_syn_count
        # Slow-target scenario: the hub aborts the timed-out transaction with
        # SYN ("armed" -> "abort_seen"); its answer is then emitted onto the
        # bus (the hub must stay passive until it has drained). The hub's
        # next telegram must not start until the drain window has passed
        # (asserted in main()).
        if self.late_phase == "armed" and self.state == "EXPECT_SYN" and b == SYN:
            self.late_phase = "abort_seen"
            self.late_abort_t = time.monotonic()
            self.late_send_at = self.late_abort_t + 0.030
        elif (self.late_phase in ("abort_seen", "done") and b != SYN
                and self.first_tx_after_late is None):
            self.first_tx_after_late = time.monotonic()
        # Everything the hub writes is bus traffic it must read back (echo).
        os.write(self.fd, bytes([b]))
        if DEBUG:
            print(f"[{time.monotonic() - self.t0:8.3f}] HUB->SIM 0x{b:02X} state={self.state} "
                  f"logical={bytes(self.logical).hex()}", file=sys.stderr)

        if self.state == "EXPECT_RESP_ACK":
            if b == ACK:
                self.state = "EXPECT_SYN"
            elif b == NAK:
                # Hub rejected our response (CRC): retransmit it.
                self.send(self.last_response)
            elif b == SYN:
                # The hub gave up on the transaction (e.g. its read was
                # garbled by a racing auto-SYN): our response is orphaned.
                # A real slave just goes back to listening.
                self.hub_abort_syns += 1
                self.state = "COLLECT"
                self.reset_telegram()
            else:
                self.errors.append(f"expected response ACK/NAK, got 0x{b:02X}")
                self.state = "COLLECT"
                self.reset_telegram()
            return

        if self.state == "EXPECT_SYN":
            if b != SYN:
                self.errors.append(f"expected SYN, got 0x{b:02X}")
            self.state = "COLLECT"
            self.reset_telegram()
            return

        # COLLECT: unescape and accumulate the request telegram.
        if self.escape_pending:
            self.escape_pending = False
            if b == 0x00:
                logical = ESC
            elif b == 0x01:
                logical = SYN
            else:
                self.errors.append(f"invalid escape sequence 0xA9 0x{b:02X}")
                self.reset_telegram()
                return
        elif b == ESC:
            self.escape_pending = True
            return
        elif b == SYN:
            # A raw SYN mid-telegram means the sending master aborted (e.g.
            # after an echo mismatch). A real eBUS receiver silently discards
            # the partial telegram and goes back to listening.
            if self.logical:
                self.hub_abort_syns += 1
                self.reset_telegram()
            return
        else:
            logical = b
        self.logical.append(logical)

        if len(self.logical) == 5:
            if self.logical[4] > 32:
                self.errors.append(f"telegram LEN too large: {self.logical[4]}; recent={bytes(self.recent_rx).hex()}")
                self.reset_telegram()
                return
            self.expected_len = 5 + self.logical[4] + 1
        if self.expected_len is not None and len(self.logical) >= self.expected_len:
            self.complete_telegram()

    def reset_telegram(self):
        self.logical = bytearray()
        self.escape_pending = False
        self.expected_len = None

    def complete_telegram(self):
        logical = bytes(self.logical)
        # The CRC covers SRC DST PB SB LEN DATA... (everything before the CRC
        # byte), computed over the wire-escaped form.
        crc_ok = logical[-1] == crc8_logical(logical[:-1])
        src, dst, pb, sb, ln = logical[0], logical[1], logical[2], logical[3], logical[4]
        data = logical[5 : 5 + ln]
        self.reset_telegram()

        if not crc_ok:
            self.errors.append(f"CRC error in telegram from 0x{src:02X}; recent={bytes(self.recent_rx).hex()}")
            self.send([NAK])
            return
        self.requests.append((src, dst, pb, sb, data))
        if src != HUB_ADDR or dst != DEVICE_ADDR:
            self.errors.append(f"unexpected addresses src=0x{src:02X} dst=0x{dst:02X}")

        # One-time slow-target scenario on one regular poll register: hold
        # the answer until well past the hub's 51 ms ACK timeout. The hub
        # must abort with SYN and then wait out its late-answer drain before
        # transmitting again (the answer is sent from run()).
        if (not self.late_answer_done and (pb, sb) == (0xB5, 0x24)
                and bytes(data) == LATE_ANSWER_REQUEST):
            self.late_answer_done = True
            self.late_phase = "armed"
            self.late_telegram_end_t = time.monotonic()
            self.late_response = bytes([ACK]) + self.build_response_for(pb, sb, data)
            self.state = "EXPECT_SYN"  # hub aborts the transaction with SYN
            self.reset_telegram()
            return

        if dst == BROADCAST:
            self.state = "EXPECT_SYN"  # hub ends a broadcast with SYN
            return

        if data == NAK_ONCE_REQUEST and not self.nak_once_done:
            # NAK once: the hub must retransmit the whole telegram.
            self.nak_once_done = True
            self.send([NAK])
            return

        self.send([ACK])
        if is_master_address(dst):
            self.state = "EXPECT_SYN"  # initiator-to-initiator: ACK only
            return

        response = self.build_response_for(pb, sb, data)
        self.last_response = response
        self.send(response)
        self.state = "EXPECT_RESP_ACK"

    # -- request answering ----------------------------------------------------

    def lookup_register(self, op, gg, rr, ii):
        if (op, gg, rr, ii) in POLL_VALUES:
            return POLL_VALUES[(op, gg, rr, ii)]
        if gg == 0x01 and rr == 0x0001:
            return U16(1)  # DHW circuit type (topology discovery)
        if gg == 0x02 and rr == 0x0002:
            return U16(1)  # circuit type (topology discovery)
        if gg == 0x03 and rr == 0x001C:
            return U8(1)  # zone circuit assignment (topology discovery)
        return None

    def build_response_for(self, pb, sb, data):
        if (pb, sb) == (0x07, 0x04):
            return build_response(IDENTIFICATION)
        if (pb, sb) == (0xB5, 0x24) and len(data) >= 1:
            op = data[0]
            if op == 0x00 and len(data) == 3:  # ReadSystemInformation
                info_id = data[1] | (data[2] << 8)
                value = SYSINFO_VALUES.get(info_id)
                if value is None:
                    return build_response(b"")
                self.sysinfo_counts[info_id] = self.sysinfo_counts.get(info_id, 0) + 1
                return build_response(F32(value))
            if op in (0x02, 0x06) and len(data) >= 6:
                ot, gg, ii = data[1], data[2], data[3]
                rr = data[4] | (data[5] << 8)
                if ot == 0x00:  # register read
                    value = self.lookup_register(op, gg, rr, ii)
                    if value is None:
                        return build_response(b"")
                    key = (op, gg, rr, ii)
                    self.poll_counts[key] = self.poll_counts.get(key, 0) + 1
                    flags = 0x03 if key in WRITABLE_KEYS else 0x01
                    return build_response(bytes([flags, gg, data[4], data[5]]) + value)
                if ot == 0x01:  # register write: short ack
                    self.writes.append((op, gg, ii, rr, bytes(data[6:])))
                    return build_response(b"")
        if (pb, sb) == (0xB5, 0x16) and len(data) >= 1:
            if data[0] == 0x10 and len(data) == 8:  # energy statistics read
                key = (data[1] & 0x0F, data[4] & 0x0F, data[5] & 0x0F)
                if key not in B516_VALUES:
                    return build_response(b"")
                self.b516_counts[key] = self.b516_counts.get(key, 0) + 1
                wh = B516_VALUES[key]
                # FLAGS: high nibble = return code (1 = no value follows).
                flags = 0x10 if wh is None else 0x00
                value = F32(0.0) if wh is None else F32(wh)
                return build_response(
                    bytes([flags, 0x00, 0x00, data[4], data[5], data[6], data[7]]) + value
                )
            if data[0] == 0x14:  # candidate current-electrical-power read
                self.b516_power_count += 1
                # status byte + f32 LE watts + four further bytes (word1).
                return build_response(bytes([0x00]) + F32(B516_POWER_W) + bytes(4))
        return build_response(b"")

    # -- main loop ------------------------------------------------------------

    def run(self, timeout_s, min_elapsed_s, done_cb):
        t0 = time.monotonic()
        deadline = t0 + timeout_s
        while not self.stop:
            now = time.monotonic()
            if now >= deadline:
                break
            if done_cb() and (now - t0) >= min_elapsed_s:
                break
            # Slave receive timeout (cf. ebusd SLAVE_RECV_TIMEOUT, scaled for
            # the slow host loop): a real eBUS receiver discards a partial
            # telegram when the inter-byte gap exceeds its receive timeout.
            # This is only a backstop: the hub's host loop can legitimately
            # stall ~40 ms mid-telegram, so the threshold sits well above
            # that (and above the ~45 ms abort->retry gap, which the abort
            # SYN already handles) and only catches truly orphaned bytes.
            if (self.state == "COLLECT" and self.logical and self.last_byte_t is not None
                    and now - self.last_byte_t > 0.100):
                self.reset_telegram()
            # Slow-target scenario: once the hub has aborted with SYN, emit
            # the held answer onto the bus. The hub has already given up; the
            # answer is an orphan on the bus and the hub must stay passive
            # until it has drained.
            if (self.late_phase == "abort_seen" and self.late_send_at is not None
                    and now >= self.late_send_at):
                self.send(self.late_response)
                self.late_phase = "done"
                if self.state != "EXPECT_SYN":  # abort SYN already consumed
                    self.state = "COLLECT"  # no ACK for an orphaned answer
            # AUTO-SYN generator: while the bus is free (no telegram being
            # collected, no response/ACK pending), a SYN is due ~40 ms after
            # the last bus activity (hub byte, simulator byte or previous
            # injection). The deadline-driven select timeout keeps the
            # cadence well inside the hub's 51 ms idle threshold, so the
            # silent-phase lockout decay never fires and the hub can only
            # start transmitting off a received SYN. The generator waits for
            # the hub's first byte: the firmware does not read the UART while
            # it boots, so early injections would pile up in the PTY buffer
            # and surface as stale SYNs during the hub's first transaction
            # (a backlog that cannot exist on a real bus). The hub's first
            # transmit uses the 51 ms silent-phase bus-free path instead.
            syn_due = None
            if (self.state == "COLLECT" and not self.logical and not self.escape_pending
                    and self.expected_len is None and self.last_hub_byte_t is not None):
                base = t0
                for ref in (self.last_hub_byte_t, self.last_sim_send_t, self.last_syn_inject):
                    if ref is not None:
                        base = max(base, ref)
                syn_due = base + 0.040
            timeout = 0.02
            if syn_due is not None:
                timeout = max(0.0, min(timeout, syn_due - now))
            try:
                r, _, _ = select.select([self.fd], [], [], timeout)
            except OSError:
                break
            if not r:
                if syn_due is not None and time.monotonic() >= syn_due:
                    # Re-check with zero timeout: if the hub started
                    # transmitting in the meantime, hold off (a real
                    # generator sees the bus activity and does not inject).
                    r2, _, _ = select.select([self.fd], [], [], 0)
                    if not r2:
                        self.last_syn_inject = time.monotonic()
                        self.syn_inject_count += 1
                        self.send([SYN])
                continue
            try:
                data = os.read(self.fd, 4096)
            except OSError:
                break
            if not data:
                break
            for b in data:
                self.handle_byte(b)


def wire_done(sim):
    for key in POLL_VALUES:
        if sim.poll_counts.get(key, 0) < 2:
            return False
    if sim.sysinfo_counts.get(0x0000, 0) < 2:  # circuit_count_maximum
        return False
    for key in B516_VALUES:
        if sim.b516_counts.get(key, 0) < 2:
            return False
    if sim.b516_power_count < 2:
        return False
    for w in EXPECTED_WRITES:
        if w not in sim.writes:
            return False
    return True


ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def main():
    binary = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_BINARY
    if not binary.exists():
        print(f"FAIL: host binary not found: {binary}")
        print("Build it first: esphome compile examples/host-vrc720-test.yaml")
        return 1

    master_fd, slave_fd = pty.openpty()
    slave_name = os.ttyname(slave_fd)
    if os.path.lexists(PTY_LINK):
        os.remove(PTY_LINK)
    os.symlink(slave_name, PTY_LINK)

    failures = []
    log_path = None
    proc = None
    try:
        log_file = tempfile.NamedTemporaryFile(
            mode="w+b", prefix="vrc720-host-test-", suffix=".log", delete=False
        )
        log_path = log_file.name
        proc = subprocess.Popen(
            [str(binary)], stdout=log_file, stderr=subprocess.STDOUT, cwd=str(REPO_ROOT)
        )

        sim = Simulator(master_fd)
        # Poll cycles at 5 s, writes at 10 s: 12 s covers two poll cycles and
        # one write round; 45 s is the hard timeout.
        sim.run(timeout_s=45.0, min_elapsed_s=12.0, done_cb=lambda: wire_done(sim))
        sim.stop = True

        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        log_file.close()

        # ---- wire-side assertions ----
        if sim.errors:
            failures.append(f"simulator errors: {sim.errors[:10]}")
        if not sim.nak_once_done:
            failures.append("NAK-once request was never seen (retry path not exercised)")
        if sim.retry_delay is None:
            failures.append("hub never retried after the injected arbitration collision")
        elif sim.retry_syn_count is None or sim.retry_syn_count < 2:
            failures.append(
                f"hub retried after {sim.retry_syn_count} AUTO-SYN(s) "
                "(arbitration lockout of 2 bus-free phases not applied)"
            )
        elif sim.retry_delay < 0.07:
            failures.append(
                f"hub retried {sim.retry_delay * 1000:.0f} ms after losing arbitration; "
                "arbitration lockout (>= 2 bus-free phases) not applied"
            )
        if not sim.late_answer_done:
            failures.append("slow-target (late answer) scenario never exercised")
        elif sim.late_phase != "done":
            failures.append(f"slow-target scenario incomplete (phase={sim.late_phase})")
        elif sim.first_tx_after_late is None:
            failures.append("hub never transmitted again after the slow-target scenario")
        elif sim.first_tx_after_late - sim.late_telegram_end_t < 0.140:
            failures.append(
                f"hub next telegram {(sim.first_tx_after_late - sim.late_telegram_end_t) * 1000:.0f} ms "
                "after the timed-out telegram ended; late-answer drain not applied "
                "(next telegram would collide with the slow target's answer)"
            )
        for key in POLL_VALUES:
            count = sim.poll_counts.get(key, 0)
            if count < 2:
                failures.append(f"register {key} answered {count} times, expected >= 2")
        if sim.sysinfo_counts.get(0x0000, 0) < 2:
            failures.append("system info 0x0000 (circuit_count_maximum) not polled twice")
        for key in B516_VALUES:
            count = sim.b516_counts.get(key, 0)
            if count < 2:
                failures.append(f"B516 statistics {key} answered {count} times, expected >= 2")
        if sim.b516_power_count < 2:
            failures.append("B516 power (sub-ID 0x14) not polled twice")
        for w in EXPECTED_WRITES:
            if w not in sim.writes:
                failures.append(f"expected write not seen on the wire: {w}")
        if not any(r[2] == 0x07 and r[3] == 0x04 for r in sim.requests):
            failures.append("identification request (07 04) never sent")
        n_b524 = sum(1 for r in sim.requests if r[2] == 0xB5 and r[3] == 0x24)
        n_b516 = sum(1 for r in sim.requests if r[2] == 0xB5 and r[3] == 0x16)
        print(f"wire: {len(sim.requests)} requests ({n_b524} B524, {n_b516} B516), "
              f"{len(sim.writes)} writes, {sum(sim.poll_counts.values())} register answers, "
              f"{sim.syn_inject_count} auto-SYNs, {sim.hub_abort_syns} hub aborts")

        # ---- log-side assertions ----
        with open(log_path, "rb") as f:
            log = ANSI_RE.sub("", f.read().decode("utf-8", errors="replace"))
        for line in EXPECTED_LOG_LINES:
            if line not in log:
                failures.append(f"log line missing: {line!r}")

        if failures:
            print(f"\nFAIL ({len(failures)} problem(s)):")
            for f_ in failures:
                print(f"  - {f_}")
            print(f"\n--- firmware log ({log_path}) tail ---")
            print("\n".join(log.splitlines()[-40:]))
            return 1
        print("PASS: eBUS hub + vaillant_vrc720 host integration test")
        print(f"(firmware log kept at {log_path})")
        return 0
    finally:
        if proc is not None and proc.poll() is None:
            proc.kill()
            proc.wait()
        os.close(master_fd)
        os.close(slave_fd)
        if os.path.lexists(PTY_LINK):
            os.remove(PTY_LINK)


if __name__ == "__main__":
    sys.exit(main())

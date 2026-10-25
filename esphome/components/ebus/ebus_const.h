#pragma once

// eBUS protocol constants (pure, no ESPHome dependencies).
//
// Reference: https://github.com/Project-Helianthus/helianthus-docs-ebus
//            (protocols/ebus-services/ebus-overview.md)

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace ebus {

// Special single-byte symbols on the wire.
static const uint8_t EBUS_SYN = 0xAA;        // end-of-message / bus idle marker
static const uint8_t EBUS_ESC = 0xA9;        // escape prefix
static const uint8_t EBUS_ACK = 0x00;        // acknowledge
static const uint8_t EBUS_NAK = 0xFF;        // negative acknowledge
static const uint8_t EBUS_BROADCAST = 0xFE;  // broadcast destination address

// Escape payload bytes: logical 0xA9 -> wire 0xA9 0x00, logical 0xAA -> wire 0xA9 0x01.
static const uint8_t EBUS_ESC_ESC = 0x00;  // escaped form of 0xA9
static const uint8_t EBUS_ESC_SYN = 0x01;  // escaped form of 0xAA

// Maximum number of data bytes in a telegram (LEN field).
static const size_t EBUS_MAX_DATA_LEN = 32;

// Wire-level timing constants in milliseconds, per the official eBUS signal
// specification (the protocol docs defer these to the standard). The values
// match the ebusd reference implementation exactly (lib/ebus/protocol.h):
// SYN_TIMEOUT 51, SIGNAL_TIMEOUT 250, SYMBOL_DURATION 5, SEND_TIMEOUT 10.
// EBUS_SLAVE_RECV_TIMEOUT_MS is deliberately more generous than ebusd's 15 ms
// so slow targets are not cut off mid-response.
static const uint32_t EBUS_SYMBOL_TIMEOUT_MS = 5;        // max duration of one symbol on the wire
static const uint32_t EBUS_SEND_TIMEOUT_MS = 10;         // wait for the echo of a sent byte
static const uint32_t EBUS_SLAVE_RECV_TIMEOUT_MS = 25;   // per-byte timeout while receiving a response
static const uint32_t EBUS_SYN_TIMEOUT_MS = 51;          // wait for ACK / first response byte / SYN
static const uint32_t EBUS_SIGNAL_TIMEOUT_MS = 250;      // no bus signal at all
static const uint32_t EBUS_BUS_IDLE_MS = 51;             // no RX activity => bus is free

// Late-answer drain window after a receive timeout (ACK/response timeout).
// Some targets (observed on a VRC720/f3) answer a request well after the
// 51 ms SYN timeout when they are busy servicing their own system master:
// the ACK/response then arrives on the bus AFTER we already gave up. If we
// start the next telegram right away, that late answer physically collides
// with our first byte. After a timeout we therefore stay passive for this
// window so the late answer can drain off the bus while we listen.
static const uint32_t EBUS_DRAIN_MS = 100;

// Arbitration delay: after the bus is observed free (standalone SYN or a
// silent idle period), wait this many microseconds before starting the own
// telegram. Yielding the first part of the bus-free phase lets competing
// initiators that grab the bus right after the SYN win cleanly. Starting
// simultaneously instead produces a skewed collision (misaligned start bits
// sample as garbage echo bytes) that costs a retry, even though the loser
// would back off anyway. 200 us within the 0-620 range is a proven default.
static const uint32_t EBUS_ARBITRATION_DELAY_US = 200;

// Retry counts. NACK retry semantics follow eBUS specification SS7.4 (also
// described in protocols/ebus-services/ebus-overview.md); the counts are not
// fixed by the protocol docs and chosen conservatively.
static const uint8_t EBUS_ACQUIRE_RETRIES = 3;  // bus lost (arbitration) retries

// Arbitration lockout, per the ebusd reference implementation of the eBUS
// specification: after a lost arbitration the initiator is barred for a
// number of bus-free phases (a received SYN, or a silent bus-idle period) so
// the winning initiator and other initiators get the bus first. The lockout
// depends on the priority class (low nibble) of the colliding address:
// same class 2, different class 3, non-initiator garbage 1. An arbitration
// attempt that times out with no echo at all does not lock out (nothing was
// won from us by a visible competitor).
static const uint8_t EBUS_LOCKOUT_SAME_PRIORITY = 2;
static const uint8_t EBUS_LOCKOUT_DIFFERENT_PRIORITY = 3;
static const uint8_t EBUS_LOCKOUT_MIN = 1;

// Optional status LED (requires a light entity, see status_led in YAML).
// The hub re-evaluates the LED state on this cadence and only re-sends the
// color when the state changed. A bus is considered dead after this much RX
// silence: a bus with an AUTO-SYN generator (like the VRC720 system) never
// goes quiet, so silence means the eBUS is not connected or stuck. On a bus
// without a SYN generator the LED shows red whenever the bus is idle.
static const uint32_t EBUS_STATUS_LED_CHECK_MS = 500;
static const uint32_t EBUS_STATUS_LED_DEAD_MS = 5000;
static const float EBUS_STATUS_LED_BRIGHTNESS = 0.25f;

// An initiator (master) address has both nibbles in {0x0, 0x1, 0x3, 0x7, 0xF}.
inline bool is_master_address(uint8_t addr) {
  auto nibble_ok = [](uint8_t n) { return n == 0x0 || n == 0x1 || n == 0x3 || n == 0x7 || n == 0xF; };
  return nibble_ok((addr >> 4) & 0x0F) && nibble_ok(addr & 0x0F);
}

// Any address that may legally appear in an address position.
inline bool is_valid_address(uint8_t addr, bool allow_broadcast = true) {
  return addr != EBUS_SYN && addr != EBUS_ESC && (allow_broadcast || addr != EBUS_BROADCAST);
}

}  // namespace ebus
}  // namespace esphome

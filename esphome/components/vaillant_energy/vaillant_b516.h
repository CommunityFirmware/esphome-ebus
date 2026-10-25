#pragma once

// Vaillant B516 energy statistics codec (PB=0xB5 SB=0x16, pure, no ESPHome
// dependencies).
//
// Sub-command 0x10 (energy statistics read), request (8 bytes):
//   10 0X FF FF 0Y 0Z DATE_LO DATE_HI
//     X = period (0 total, 1 daily, 2 monthly, 3 yearly)
//     Y = source (1 solar, 2 environmental, 3 electrical, 4 fuel)
//     Z = usage (0 sum, 3 heating, 4 DHW, 5 cooling)
//     DATE = little-endian packed word: day | month << 5 | (year-2000) << 9
// Reply (11 bytes):
//   FLAGS PERIOD_IDX ENERGY_IDX 0Y 0Z DATE_LO DATE_HI <f32 LE watt-hours>
//   FLAGS high nibble = return code (0 = value present, non-zero = no value).
//
// Sub-ID 0x14 (current electrical power, candidate): request is the single
// byte 0x14; reply is a status byte, an f32 LE value (candidate watts) and
// possibly four further bytes. The requester rejects replies of 7 bytes or
// less.
//
// Reference: https://github.com/Project-Helianthus/helianthus-docs-ebus
//            protocols/vaillant/ebus-vaillant-B516-energy.md

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome {
namespace vaillant {

// B516 service bytes.
static const uint8_t B516_PB = 0xB5;
static const uint8_t B516_SB = 0x16;

// B516 sub-IDs.
static const uint8_t B516_SUB_STATISTICS = 0x10;
static const uint8_t B516_SUB_POWER = 0x14;

// Period selector (low nibble of request byte 1).
static const uint8_t B516_PERIOD_TOTAL = 0x00;
static const uint8_t B516_PERIOD_DAILY = 0x01;

// Pack a calendar date into the little-endian request date word.
uint16_t b516_pack_date(uint16_t year, uint8_t month, uint8_t day);

// Build the 8-byte energy statistics request payload.
std::vector<uint8_t> b516_build_statistics_request(uint8_t period, uint8_t source, uint8_t usage,
                                                   uint16_t packed_date);

struct B516StatisticsReply {
  bool valid{false};   // reply has the expected shape
  bool ok{false};      // return code 0: a trustworthy value follows
  float wh{0.0f};     // watt-hours (only meaningful when ok)
};

B516StatisticsReply b516_parse_statistics_reply(const std::vector<uint8_t> &data);

// Sub-ID 0x14 reply: status byte + f32 LE (candidate watts). Replies of 7
// bytes or less are rejected per the protocol notes.
bool b516_parse_power_reply(const std::vector<uint8_t> &data, float *watts);

}  // namespace vaillant
}  // namespace esphome

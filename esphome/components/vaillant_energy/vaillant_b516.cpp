#include "vaillant_b516.h"

#include <cstring>

namespace esphome {
namespace vaillant {

uint16_t b516_pack_date(uint16_t year, uint8_t month, uint8_t day) {
  const uint16_t year_offset = (year >= 2000) ? (year - 2000) : 0;
  return static_cast<uint16_t>(((year_offset & 0x7F) << 9) | ((month & 0x0F) << 5) | (day & 0x1F));
}

std::vector<uint8_t> b516_build_statistics_request(uint8_t period, uint8_t source, uint8_t usage,
                                                   uint16_t packed_date) {
  return {B516_SUB_STATISTICS,
          static_cast<uint8_t>(period & 0x0F),
          0xFF,
          0xFF,
          static_cast<uint8_t>(source & 0x0F),
          static_cast<uint8_t>(usage & 0x0F),
          static_cast<uint8_t>(packed_date & 0xFF),
          static_cast<uint8_t>((packed_date >> 8) & 0xFF)};
}

static float decode_f32_le_(const uint8_t *bytes) {
  uint32_t raw = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
                 (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

B516StatisticsReply b516_parse_statistics_reply(const std::vector<uint8_t> &data) {
  B516StatisticsReply reply;
  if (data.size() < 11) {
    return reply;
  }
  reply.valid = true;
  reply.ok = (data[0] >> 4) == 0;
  if (reply.ok) {
    reply.wh = decode_f32_le_(&data[7]);
  }
  return reply;
}

bool b516_parse_power_reply(const std::vector<uint8_t> &data, float *watts) {
  if (data.size() <= 7) {
    return false;
  }
  *watts = decode_f32_le_(&data[1]);
  return true;
}

}  // namespace vaillant
}  // namespace esphome

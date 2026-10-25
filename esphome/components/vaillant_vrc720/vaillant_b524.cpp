#include "vaillant_b524.h"

#include <cmath>
#include <cstring>

namespace esphome {
namespace vaillant {

std::vector<uint8_t> b524_build_read_request(uint8_t op, uint8_t gg, uint8_t ii, uint16_t rr) {
  return {op, B524_OT_READ, gg, ii, static_cast<uint8_t>(rr & 0xFF), static_cast<uint8_t>(rr >> 8)};
}

std::vector<uint8_t> b524_build_write_request(uint8_t op, uint8_t gg, uint8_t ii, uint16_t rr,
                                              const std::vector<uint8_t> &value) {
  std::vector<uint8_t> payload = {op, B524_OT_WRITE, gg, ii, static_cast<uint8_t>(rr & 0xFF),
                                  static_cast<uint8_t>(rr >> 8)};
  payload.insert(payload.end(), value.begin(), value.end());
  return payload;
}

std::vector<uint8_t> b524_build_system_info_request(uint16_t id) {
  return {B524_OP_READ_SYSTEM_INFORMATION, static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8)};
}

B524RegisterReply b524_parse_register_reply(const std::vector<uint8_t> &data, uint8_t expected_gg,
                                            uint16_t expected_rr) {
  B524RegisterReply reply;
  // Short responses (< 4 bytes) are not successful register reads.
  if (data.size() < 4) {
    return reply;
  }
  reply.flags = data[0];
  reply.gg = data[1];
  reply.rr = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
  reply.value.assign(data.begin() + 4, data.end());
  reply.valid = (reply.gg == expected_gg) && (reply.rr == expected_rr);
  return reply;
}

bool b524_parse_system_info_reply(const std::vector<uint8_t> &data, float *out) {
  if (data.size() != 4) {
    return false;
  }
  uint32_t raw = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                 (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
  float value;
  memcpy(&value, &raw, sizeof(value));
  if (std::isnan(value)) {
    return false;
  }
  *out = value;
  return true;
}

// ---- value codecs ---------------------------------------------------------

bool b524_decode_f32(const std::vector<uint8_t> &value, float *out) {
  if (value.size() != 4) {
    return false;
  }
  uint32_t raw = static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
                 (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
  float result;
  memcpy(&result, &raw, sizeof(result));
  if (std::isnan(result)) {
    return false;  // NaN sentinel: value unavailable
  }
  *out = result;
  return true;
}

bool b524_decode_u16(const std::vector<uint8_t> &value, uint16_t *out) {
  if (value.size() != 2) {
    return false;
  }
  *out = static_cast<uint16_t>(value[0]) | (static_cast<uint16_t>(value[1]) << 8);
  return true;
}

bool b524_decode_u8(const std::vector<uint8_t> &value, uint8_t *out) {
  if (value.size() != 1) {
    return false;
  }
  *out = value[0];
  return true;
}

bool b524_decode_i8(const std::vector<uint8_t> &value, int8_t *out) {
  if (value.size() != 1) {
    return false;
  }
  *out = static_cast<int8_t>(value[0]);
  return true;
}

bool b524_decode_u32(const std::vector<uint8_t> &value, uint32_t *out) {
  if (value.size() != 4) {
    return false;
  }
  uint32_t raw = static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8) |
                 (static_cast<uint32_t>(value[2]) << 16) | (static_cast<uint32_t>(value[3]) << 24);
  if (raw == 0x7FFFFFFF) {
    return false;  // no-data sentinel
  }
  *out = raw;
  return true;
}

bool b524_decode_bool(const std::vector<uint8_t> &value, bool *out) {
  if (value.size() == 1) {
    *out = value[0] != 0;
    return true;
  }
  if (value.size() == 2) {
    uint16_t raw;
    if (!b524_decode_u16(value, &raw)) {
      return false;
    }
    *out = raw != 0;
    return true;
  }
  return false;
}

std::string b524_decode_string(const std::vector<uint8_t> &value) {
  std::string result;
  for (uint8_t b : value) {
    if (b == 0x00) {
      break;  // NUL-terminated
    }
    result.push_back(static_cast<char>(b));
  }
  return result;
}

static uint8_t bcd_to_dec(uint8_t bcd) { return static_cast<uint8_t>(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F)); }

bool b524_decode_date(const std::vector<uint8_t> &value, std::string *out) {
  if (value.size() != 3) {
    return false;
  }
  uint8_t day = bcd_to_dec(value[0]);
  uint8_t month = bcd_to_dec(value[1]);
  uint16_t year = 2000 + bcd_to_dec(value[2]);
  if (day < 1 || day > 31 || month < 1 || month > 12) {
    return false;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", year, month, day);
  *out = buf;
  return true;
}

bool b524_decode_time(const std::vector<uint8_t> &value, std::string *out) {
  if (value.size() != 3 && value.size() != 2) {
    return false;
  }
  uint8_t hour = value[0];
  uint8_t minute = value[1];
  uint8_t second = value.size() == 3 ? value[2] : 0;
  if (hour > 23 || minute > 59 || second > 59) {
    return false;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", hour, minute, second);
  *out = buf;
  return true;
}

bool b524_decode_version(const std::vector<uint8_t> &value, std::string *out) {
  if (value.size() != 3) {
    return false;
  }
  // Byte-decimal (not BCD): each byte is one decimal component.
  if (value[0] > 99 || value[1] > 99 || value[2] > 99) {
    return false;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u", value[0], value[1], value[2]);
  *out = buf;
  return true;
}

void b524_encode_f32(float value, std::vector<uint8_t> *out) {
  uint32_t raw;
  memcpy(&raw, &value, sizeof(raw));
  out->push_back(static_cast<uint8_t>(raw & 0xFF));
  out->push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
  out->push_back(static_cast<uint8_t>((raw >> 16) & 0xFF));
  out->push_back(static_cast<uint8_t>((raw >> 24) & 0xFF));
}

void b524_encode_u16(uint16_t value, std::vector<uint8_t> *out) {
  out->push_back(static_cast<uint8_t>(value & 0xFF));
  out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void b524_encode_u8(uint8_t value, std::vector<uint8_t> *out) { out->push_back(value); }

// ---- identification (service 0x07 0x04) -----------------------------------

B524Identification b524_parse_identification(const std::vector<uint8_t> &data) {
  B524Identification id;
  if (data.size() != 10) {
    return id;
  }
  id.manufacturer = data[0];
  id.device_id.assign(reinterpret_cast<const char *>(&data[1]), 5);
  id.sw_version = data[6];
  id.sw_revision = data[7];
  // SPN: four-digit BCD number, least-significant byte first.
  // SPN = BCD(byte 8) + 100 * BCD(byte 9); wire "17 04" -> 417 (0x01A1).
  id.spn = static_cast<uint16_t>(bcd_to_dec(data[8]) + 100 * bcd_to_dec(data[9]));
  id.valid = true;
  return id;
}

std::string b524_identification_model(const std::string &device_id, uint16_t spn) {
  // VRC720-family crosswalk (EID, SPN) -> model.
  // Source: helianthus-docs-ebus protocols/vaillant/ebus-vaillant-regulators.md
  static const struct {
    const char *eid;
    uint16_t spn;
    const char *model;
  } CROSSWALK[] = {
      {"70000", 0x0141, "VRC700 R1"},   {"70000", 0x0155, "VRC700 R2"}, {"70000", 0x015A, "VRC700 R4"},
      {"70000", 0x016C, "VRC700 R5"},   {"70000", 0x0171, "VRC700 R6"}, {"70000", 0x01D8, "VRC700 R6"},
      {"72000", 0x0179, "VRC720"},      {"B7V00", 0x0163, "VRC700 R4"}, {"BASS0", 0x0184, "VRC720"},
      {"BASS0", 0x0193, "VRT380"},      {"BASS2", 0x01A1, "VRC720"},    {"BASS2", 0x01CF, "VRC720"},
      {"BASS3", 0x01B0, "VRT380"},      {"BASS3", 0x01BB, "VRC720"},    {"BASS3", 0x01D0, "VRT380"},
      {"BASS3", 0x01D9, "VRC720"},      {"BASV0", 0x0184, "VRC720"},    {"BASV0", 0x0193, "VRT380"},
      {"BASV2", 0x01A1, "VRC720"},      {"BASV2", 0x01CF, "VRC720"},    {"BASV3", 0x01B0, "VRT380"},
      {"BASV3", 0x01BB, "VRC720"},      {"BASV3", 0x01D0, "VRT380"},    {"BASV3", 0x01D9, "VRC720"},
      {"CTLS0", 0x0188, "VRT380"},      {"CTLS0", 0x0189, "VRC720"},    {"CTLS2", 0x019D, "VRC720"},
      {"CTLS3", 0x01AF, "VRT380"},      {"CTLS3", 0x01B6, "VRC720"},    {"CTLS3", 0x01E1, "VRC720"},
      {"CTLV0", 0x0187, "VRT380"},      {"CTLV2", 0x019B, "VRC720"},    {"CTLV3", 0x01AE, "VRT380"},
      {"CTLV3", 0x01B5, "VRC720"},      {"CTLV3", 0x01DC, "VRC720"},    {"CTLV3", 0x01E0, "VRC720"},
      {"CTLX0", 0x0194, "VR940"},       {"EMM00", 0x0181, "VRC710"},
  };
  for (const auto &row : CROSSWALK) {
    if (device_id == row.eid && spn == row.spn) {
      return row.model;
    }
  }
  return "";
}

}  // namespace vaillant
}  // namespace esphome

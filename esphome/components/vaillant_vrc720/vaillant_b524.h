#pragma once

// Vaillant B524 (GetExtendedRegisters, PB=0xB5 SB=0x24) protocol codec
// (pure, no ESPHome dependencies).
//
// Request payloads:
//   OP=0x02/0x06 read:  OP OT GG II RRlo RRhi          (OT=0x00)
//   OP=0x02/0x06 write: OP OT GG II RRlo RRhi VALUE... (OT=0x01)
//   OP=0x00 ReadSystemInformation: 00 IDlo IDhi
//
// Register read reply (OP=0x02/0x06): FLAGS GG RRlo RRhi VALUE...
//   (II is NOT echoed; correlation needs the request context.)
// System information reply (OP=0x00): four float32 little-endian bytes.
//
// Reference: https://github.com/Project-Helianthus/helianthus-docs-ebus
//            (protocols/vaillant/ebus-vaillant-B524.md and register map)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace vaillant {

// B524 service bytes.
static const uint8_t B524_PB = 0xB5;
static const uint8_t B524_SB = 0x24;

// B524 opcodes (OP byte).
static const uint8_t B524_OP_READ_SYSTEM_INFORMATION = 0x00;
static const uint8_t B524_OP_GET_PARAMETER = 0x02;         // local selector family
static const uint8_t B524_OP_GET_DEVICE_PARAMETER = 0x06;  // controller-mediated family

// B524 operation types (OT byte).
static const uint8_t B524_OT_READ = 0x00;
static const uint8_t B524_OT_WRITE = 0x01;

// Value types used by the register catalog.
enum class B524ValueType : uint8_t {
  F32,     // IEEE 754 float32, little-endian (VRC720); NaN = unavailable
  U16,     // uint16 little-endian
  U8,      // uint8
  I8,      // int8
  U32,     // uint32 little-endian; 0x7FFFFFFF = unavailable
  BOOL,    // u16 (0/1) or u8 (0/1)
  STRING,  // NUL-terminated ASCII
  ENUM16,  // uint16 little-endian enumeration code
  DATE,    // BCD DD MM YY (year = 2000 + YY)
  TIME,    // plain HH MM SS bytes
  VERSION, // 3 bytes major.minor.patch, byte-decimal (not BCD)
  RAW,     // opaque bytes
};

// Build a B524 read request payload: OP 00 GG II RRlo RRhi.
std::vector<uint8_t> b524_build_read_request(uint8_t op, uint8_t gg, uint8_t ii, uint16_t rr);

// Build a B524 write request payload: OP 01 GG II RRlo RRhi VALUE...
std::vector<uint8_t> b524_build_write_request(uint8_t op, uint8_t gg, uint8_t ii, uint16_t rr,
                                              const std::vector<uint8_t> &value);

// Build an OP=0x00 ReadSystemInformation request payload: 00 IDlo IDhi.
std::vector<uint8_t> b524_build_system_info_request(uint16_t id);

// A parsed B524 register read reply.
struct B524RegisterReply {
  bool valid{false};  // value-shaped reply with a matching header (>= 4 bytes)
  uint8_t flags{0};   // attribute byte (bit0=Visible, bit1=RW candidate)
  uint8_t gg{0};
  uint16_t rr{0};
  std::vector<uint8_t> value;  // value bytes only
};

// Parse a register read reply (OP=0x02/0x06). `expected_gg`/`expected_rr` must
// match the reply header for the reply to be considered valid. Replies shorter
// than 4 bytes are not successful register reads.
B524RegisterReply b524_parse_register_reply(const std::vector<uint8_t> &data, uint8_t expected_gg,
                                            uint16_t expected_rr);

// Parse an OP=0x00 ReadSystemInformation reply (four float32 LE bytes).
bool b524_parse_system_info_reply(const std::vector<uint8_t> &data, float *out);

// ---- value codecs ---------------------------------------------------------

// All decode functions return false when the value is unavailable (NaN / sentinel)
// or has the wrong width.
bool b524_decode_f32(const std::vector<uint8_t> &value, float *out);
bool b524_decode_u16(const std::vector<uint8_t> &value, uint16_t *out);
bool b524_decode_u8(const std::vector<uint8_t> &value, uint8_t *out);
bool b524_decode_i8(const std::vector<uint8_t> &value, int8_t *out);
bool b524_decode_u32(const std::vector<uint8_t> &value, uint32_t *out);
bool b524_decode_bool(const std::vector<uint8_t> &value, bool *out);
std::string b524_decode_string(const std::vector<uint8_t> &value);
// Date as ISO string "YYYY-MM-DD" (BCD DD MM YY, year = 2000 + YY).
bool b524_decode_date(const std::vector<uint8_t> &value, std::string *out);
// Time as "HH:MM:SS" (plain bytes).
bool b524_decode_time(const std::vector<uint8_t> &value, std::string *out);
// Firmware version as "major.minor.patch" (3 plain decimal bytes, not BCD).
bool b524_decode_version(const std::vector<uint8_t> &value, std::string *out);

void b524_encode_f32(float value, std::vector<uint8_t> *out);    // 4 bytes LE
void b524_encode_u16(uint16_t value, std::vector<uint8_t> *out); // 2 bytes LE
void b524_encode_u8(uint8_t value, std::vector<uint8_t> *out);   // 1 byte

// ---- identification (service 0x07 0x04) -----------------------------------

struct B524Identification {
  bool valid{false};
  uint8_t manufacturer{0};
  std::string device_id;  // 5 ASCII characters
  uint8_t sw_version{0};  // BCD
  uint8_t sw_revision{0}; // BCD
  uint16_t spn{0};        // software product number (BCD, LSB-first)
};

// Parse a 10-byte identification reply.
B524Identification b524_parse_identification(const std::vector<uint8_t> &data);

// Look up the model name for an (device_id, SPN) pair in the VRC720-family
// crosswalk. Returns an empty string for unknown pairs.
std::string b524_identification_model(const std::string &device_id, uint16_t spn);

}  // namespace vaillant
}  // namespace esphome

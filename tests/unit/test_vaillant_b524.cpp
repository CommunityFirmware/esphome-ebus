// Standalone unit tests for the pure Vaillant B524 codec (no ESPHome dependencies).
// Build & run: tests/unit/run_tests.sh

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../esphome/components/vaillant_vrc720/vaillant_b524.h"

using namespace esphome::vaillant;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) \
  do { \
    g_checks++; \
    if (!(cond)) { \
      g_failures++; \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
  } while (0)

static bool vec_eq(const std::vector<uint8_t> &a, std::initializer_list<uint8_t> b) {
  return a == std::vector<uint8_t>(b);
}

static void test_read_request() {
  // Read system register 0x0034 (date): OP=0x02 OT=0x00 GG=0x00 II=0x00 RR=0x0034
  CHECK(vec_eq(b524_build_read_request(0x02, 0x00, 0x00, 0x0034), {0x02, 0x00, 0x00, 0x00, 0x34, 0x00}));
  // Read circuit 1 register 0x0008: GG=0x02 II=0x01
  CHECK(vec_eq(b524_build_read_request(0x02, 0x02, 0x01, 0x0008), {0x02, 0x00, 0x02, 0x01, 0x08, 0x00}));
  // Read device parameter (OP=0x06) VR71 slot 1 register 0x0001
  CHECK(vec_eq(b524_build_read_request(0x06, 0x0C, 0x01, 0x0001), {0x06, 0x00, 0x0C, 0x01, 0x01, 0x00}));
  // 16-bit register address is little-endian
  CHECK(vec_eq(b524_build_read_request(0x02, 0x00, 0x00, 0x00F6), {0x02, 0x00, 0x00, 0x00, 0xF6, 0x00}));
}

static void test_write_request() {
  // DHW temperature setpoint write: 02 01 01 FF 04 00 <f32 LE>
  std::vector<uint8_t> value;
  b524_encode_f32(55.0f, &value);
  CHECK(vec_eq(value, {0x00, 0x00, 0x5C, 0x42}));  // 55.0f = 0x425C0000
  auto req = b524_build_write_request(0x02, 0x01, 0xFF, 0x0004, value);
  CHECK(vec_eq(req, {0x02, 0x01, 0x01, 0xFF, 0x04, 0x00, 0x00, 0x00, 0x5C, 0x42}));

  // Zone operating mode write: 02 01 03 II 06 00 V0 V1 (u16 LE)
  std::vector<uint8_t> mode;
  b524_encode_u16(2, &mode);
  req = b524_build_write_request(0x02, 0x03, 0x02, 0x0006, mode);
  CHECK(vec_eq(req, {0x02, 0x01, 0x03, 0x02, 0x06, 0x00, 0x02, 0x00}));

  // One-time DHW load command: 02 01 01 FF 0D 00 06
  std::vector<uint8_t> one;
  b524_encode_u8(6, &one);
  req = b524_build_write_request(0x02, 0x01, 0xFF, 0x000D, one);
  CHECK(vec_eq(req, {0x02, 0x01, 0x01, 0xFF, 0x0D, 0x00, 0x06}));
}

static void test_system_info_request() {
  CHECK(vec_eq(b524_build_system_info_request(0x0000), {0x00, 0x00, 0x00}));
  CHECK(vec_eq(b524_build_system_info_request(0x0006), {0x00, 0x06, 0x00}));
  CHECK(vec_eq(b524_build_system_info_request(0x0012), {0x00, 0x12, 0x00}));
}

static void test_parse_register_reply() {
  // Real captured vectors from the spec:
  //   req 02 00 0A 00 01 00 -> reply 03 0A 01 00 00
  auto reply = b524_parse_register_reply({0x03, 0x0A, 0x01, 0x00, 0x00}, 0x0A, 0x0001);
  CHECK(reply.valid);
  CHECK(reply.flags == 0x03);
  CHECK(reply.gg == 0x0A);
  CHECK(reply.rr == 0x0001);
  CHECK(vec_eq(reply.value, {0x00}));

  //   req 06 00 0C 01 01 00 -> reply 01 0C 01 00 01
  reply = b524_parse_register_reply({0x01, 0x0C, 0x01, 0x00, 0x01}, 0x0C, 0x0001);
  CHECK(reply.valid);
  CHECK(reply.flags == 0x01);
  CHECK(vec_eq(reply.value, {0x01}));

  // f32 reply: FLAGS GG RRlo RRhi <f32 LE>
  reply = b524_parse_register_reply({0x03, 0x00, 0x4B, 0x00, 0x00, 0x00, 0xAC, 0x41}, 0x00, 0x004B);
  CHECK(reply.valid);
  CHECK(reply.value.size() == 4);

  // Short replies (< 4 bytes) are not successful reads.
  reply = b524_parse_register_reply({0x00}, 0x00, 0x0001);
  CHECK(!reply.valid);
  reply = b524_parse_register_reply({}, 0x00, 0x0001);
  CHECK(!reply.valid);
  reply = b524_parse_register_reply({0x01, 0x02, 0x03}, 0x00, 0x0001);
  CHECK(!reply.valid);

  // Header mismatch (GG or RR) => not a valid reply for this request.
  reply = b524_parse_register_reply({0x03, 0x0A, 0x01, 0x00, 0x00}, 0x0B, 0x0001);
  CHECK(!reply.valid);
  reply = b524_parse_register_reply({0x03, 0x0A, 0x01, 0x00, 0x00}, 0x0A, 0x0002);
  CHECK(!reply.valid);
}

static void test_parse_system_info_reply() {
  float out = 0;
  // circuit_count_maximum = 3.0f (0x40400000 LE: 00 00 40 40)
  CHECK(b524_parse_system_info_reply({0x00, 0x00, 0x40, 0x40}, &out));
  CHECK(out == 3.0f);
  // NaN => unavailable
  CHECK(!b524_parse_system_info_reply({0x00, 0x00, 0xC0, 0x7F}, &out));
  // wrong width
  CHECK(!b524_parse_system_info_reply({0x00, 0x00, 0x40}, &out));
  CHECK(!b524_parse_system_info_reply({0x00, 0x00, 0x40, 0x40, 0x00}, &out));
}

static void test_decode_f32() {
  float out = 0;
  CHECK(b524_decode_f32({0x00, 0x00, 0xAC, 0x41}, &out));
  CHECK(out == 21.5f);
  CHECK(b524_decode_f32({0x00, 0x00, 0x5C, 0x42}, &out));
  CHECK(out == 55.0f);
  // negative temperature -3.5f = 0xC0600000 LE: 00 00 60 C0
  CHECK(b524_decode_f32({0x00, 0x00, 0x60, 0xC0}, &out));
  CHECK(out == -3.5f);
  // NaN sentinel => unavailable
  CHECK(!b524_decode_f32({0x00, 0x00, 0xC0, 0x7F}, &out));
  // wrong width
  CHECK(!b524_decode_f32({0x00, 0x00, 0xAC}, &out));
  CHECK(!b524_decode_f32({0x00, 0x00, 0xAC, 0x41, 0x00}, &out));
}

static void test_decode_integers() {
  uint16_t u16;
  CHECK(b524_decode_u16({0x01, 0x00}, &u16) && u16 == 1);
  CHECK(b524_decode_u16({0xFF, 0xFF}, &u16) && u16 == 0xFFFF);
  CHECK(!b524_decode_u16({0x01}, &u16));

  uint8_t u8;
  CHECK(b524_decode_u8({0x2A}, &u8) && u8 == 42);
  CHECK(!b524_decode_u8({0x2A, 0x00}, &u8));

  int8_t i8;
  CHECK(b524_decode_i8({0xFF}, &i8) && i8 == -1);  // zone not bound
  CHECK(b524_decode_i8({0x02}, &i8) && i8 == 2);
  CHECK(!b524_decode_i8({0x02, 0x00}, &i8));

  uint32_t u32;
  CHECK(b524_decode_u32({0x10, 0x27, 0x00, 0x00}, &u32) && u32 == 10000);
  CHECK(!b524_decode_u32({0xFF, 0xFF, 0xFF, 0x7F}, &u32));  // sentinel
  CHECK(!b524_decode_u32({0x10, 0x27}, &u32));
}

static void test_decode_bool() {
  bool b = false;
  CHECK(b524_decode_bool({0x01}, &b) && b);
  CHECK(b524_decode_bool({0x00}, &b) && !b);
  CHECK(b524_decode_bool({0x01, 0x00}, &b) && b);   // u16 LE
  CHECK(b524_decode_bool({0x00, 0x00}, &b) && !b);
  CHECK(!b524_decode_bool({0x01, 0x00, 0x00}, &b));
}

static void test_decode_string() {
  CHECK(b524_decode_string({'Z', 'o', 'n', 'e', '1', 0x00}) == "Zone1");
  CHECK(b524_decode_string({'A', 'B'}) == "AB");
  CHECK(b524_decode_string({0x00}) == "");
  CHECK(b524_decode_string({}) == "");
}

static void test_decode_date_time() {
  std::string out;
  // BCD 16 0A 26 -> 2026-10-16
  CHECK(b524_decode_date({0x16, 0x0A, 0x26}, &out));
  CHECK(out == "2026-10-16");
  // BCD 01 01 0F -> 2015-01-01 (factory default sentinel)
  CHECK(b524_decode_date({0x01, 0x01, 0x0F}, &out));
  CHECK(out == "2015-01-01");
  CHECK(!b524_decode_date({0x16, 0x0A}, &out));
  CHECK(!b524_decode_date({0x32, 0x0A, 0x1A}, &out));  // day 32 invalid

  CHECK(b524_decode_time({0x0E, 0x1E, 0x05}, &out));
  CHECK(out == "14:30:05");
  CHECK(b524_decode_time({0x00, 0x00, 0x00}, &out));
  CHECK(out == "00:00:00");
  CHECK(!b524_decode_time({0x18, 0x00, 0x00}, &out));  // hour 24 invalid
  CHECK(!b524_decode_time({0x0E}, &out));
}

static void test_decode_version() {
  std::string out;
  // Byte-decimal (not BCD): 07 04 12 -> "7.4.18"
  CHECK(b524_decode_version({0x07, 0x04, 0x12}, &out));
  CHECK(out == "7.4.18");
  // 0x0A is decimal 10, 0x1F is decimal 31
  CHECK(b524_decode_version({0x02, 0x0A, 0x1F}, &out));
  CHECK(out == "2.10.31");
  CHECK(b524_decode_version({0x00, 0x00, 0x00}, &out));
  CHECK(out == "0.0.0");
  CHECK(!b524_decode_version({0x07, 0x04}, &out));       // too short
  CHECK(!b524_decode_version({0x07, 0x04, 0x12, 0x00}, &out));  // too long
  CHECK(!b524_decode_version({0x64, 0x00, 0x00}, &out));  // 100 > 99 invalid
}

static void test_encode_roundtrip() {
  std::vector<uint8_t> buf;
  b524_encode_f32(-12.25f, &buf);
  float f;
  CHECK(b524_decode_f32(buf, &f) && f == -12.25f);

  buf.clear();
  b524_encode_u16(0x1234, &buf);
  uint16_t u16;
  CHECK(b524_decode_u16(buf, &u16) && u16 == 0x1234);

  buf.clear();
  b524_encode_u8(0xAB, &buf);
  uint8_t u8;
  CHECK(b524_decode_u8(buf, &u8) && u8 == 0xAB);
}

static void test_identification() {
  // VRC720: manufacturer 0xB5, "BASS2", sw 07 04, SPN field 17 04 -> 417 (0x01A1)
  std::vector<uint8_t> data = {0xB5, 'B', 'A', 'S', 'S', '2', 0x07, 0x04, 0x17, 0x04};
  auto id = b524_parse_identification(data);
  CHECK(id.valid);
  CHECK(id.manufacturer == 0xB5);
  CHECK(id.device_id == "BASS2");
  CHECK(id.sw_version == 0x07);
  CHECK(id.sw_revision == 0x04);
  CHECK(id.spn == 417);
  CHECK(b524_identification_model(id.device_id, id.spn) == "VRC720");

  // SPN LSB-first BCD: "63 04" -> 463 (0x01CF)
  data = {0xB5, 'B', 'A', 'S', 'S', '2', 0x05, 0x07, 0x63, 0x04};
  id = b524_parse_identification(data);
  CHECK(id.valid && id.spn == 463);
  CHECK(b524_identification_model(id.device_id, id.spn) == "VRC720");

  // EMM00 / SPN 0x0181 (decimal 385, wire BCD LSB-first "85 03") -> VRC710
  data = {0xB5, 'E', 'M', 'M', '0', '0', 0x01, 0x00, 0x85, 0x03};
  id = b524_parse_identification(data);
  CHECK(id.valid && id.spn == 0x0181);
  CHECK(b524_identification_model(id.device_id, id.spn) == "VRC710");

  // Unknown pair -> empty model
  CHECK(b524_identification_model("XXXXX", 0x0001) == "");
  CHECK(b524_identification_model("BASS2", 0x0001) == "");

  // Wrong length -> invalid
  id = b524_parse_identification({0xB5, 'B', 'A', 'S', 'S', '2', 0x07, 0x04, 0x17});
  CHECK(!id.valid);
}

int main() {
  test_read_request();
  test_write_request();
  test_system_info_request();
  test_parse_register_reply();
  test_parse_system_info_reply();
  test_decode_f32();
  test_decode_integers();
  test_decode_bool();
  test_decode_string();
  test_decode_date_time();
  test_decode_version();
  test_encode_roundtrip();
  test_identification();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

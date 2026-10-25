// Standalone unit tests for the pure Vaillant B516 codec (no ESPHome
// dependencies). Build & run: tests/unit/run_tests.sh

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../esphome/components/vaillant_energy/vaillant_b516.h"

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

static void test_pack_date() {
  // Worked-evidence and doc examples (ebus-vaillant-B516-energy.md 4.3-4.5).
  CHECK(b516_pack_date(2025, 1, 1) == 0x3221);   // bytes 21 32
  CHECK(b516_pack_date(2025, 12, 31) == 0x339F);  // bytes 9F 33
  CHECK(b516_pack_date(2026, 4, 7) == 0x3487);   // bytes 87 34
  CHECK(b516_pack_date(2025, 3, 0) == 0x3260);   // monthly: day = 0
  CHECK(b516_pack_date(2025, 0, 0) == 0x3200);   // yearly: day = month = 0
  CHECK(b516_pack_date(2024, 0, 0) == 0x3000);
  CHECK(b516_pack_date(2026, 0, 0) == 0x3400);
}

static void test_statistics_request() {
  // Total, electrical heating (worked evidence query 1, canonical zero date).
  CHECK(vec_eq(b516_build_statistics_request(0x00, 0x03, 0x03, 0x0000),
               {0x10, 0x00, 0xFF, 0xFF, 0x03, 0x03, 0x00, 0x00}));
  // Daily, electrical heating, 1 January 2025 (doc section 7 example shape).
  CHECK(vec_eq(b516_build_statistics_request(0x01, 0x03, 0x03, 0x3221),
               {0x10, 0x01, 0xFF, 0xFF, 0x03, 0x03, 0x21, 0x32}));
  // Yearly, gas heating, year 2025.
  CHECK(vec_eq(b516_build_statistics_request(0x03, 0x04, 0x03, 0x3200),
               {0x10, 0x03, 0xFF, 0xFF, 0x04, 0x03, 0x00, 0x32}));
}

static void test_statistics_reply() {
  // Worked evidence query 1: total electrical heating = 14165.0 Wh.
  auto ok_reply = b516_parse_statistics_reply(
      {0x00, 0x00, 0x00, 0x03, 0x03, 0x87, 0x34, 0x00, 0x54, 0x5D, 0x46});
  CHECK(ok_reply.valid);
  CHECK(ok_reply.ok);
  CHECK(ok_reply.wh == 14165.0f);

  // Worked evidence query 2: electrical DHW not available (return code 1).
  auto nok_reply = b516_parse_statistics_reply(
      {0x10, 0x00, 0xFF, 0x03, 0x04, 0x87, 0x34, 0x00, 0x00, 0x00, 0x00});
  CHECK(nok_reply.valid);
  CHECK(!nok_reply.ok);

  // Worked evidence query 3: yearly 2025 not available (return code 1).
  auto nok_year = b516_parse_statistics_reply(
      {0x13, 0xFF, 0xFF, 0x03, 0x03, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00});
  CHECK(nok_year.valid);
  CHECK(!nok_year.ok);

  // Truncated replies are invalid.
  CHECK(!b516_parse_statistics_reply({0x00, 0x00, 0x00, 0x03, 0x03, 0x87, 0x34, 0x00, 0x54, 0x5D}).valid);
  CHECK(!b516_parse_statistics_reply({}).valid);
}

static void test_power_reply() {
  // Status byte + f32 LE (1250.0 W = 0x449C4000) + four further bytes.
  float watts = 0.0f;
  CHECK(b516_parse_power_reply({0x00, 0x00, 0x40, 0x9C, 0x44, 0x00, 0x00, 0x00, 0x00}, &watts));
  CHECK(watts == 1250.0f);

  // The requester rejects replies of 7 bytes or less.
  CHECK(!b516_parse_power_reply({0x00, 0x00, 0x40, 0x9C, 0x44, 0x00, 0x00}, &watts));
  CHECK(!b516_parse_power_reply({}, &watts));
}

int main() {
  test_pack_date();
  test_statistics_request();
  test_statistics_reply();
  test_power_reply();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

// Standalone unit tests for the pure eBUS frame codec (no ESPHome dependencies).
// Build & run: tests/unit/run_tests.sh

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../esphome/components/ebus/ebus_frame.h"

using namespace esphome::ebus;

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

#define CHECK_EQ_HEX(actual, expected) \
  do { \
    g_checks++; \
    if ((actual) != (expected)) { \
      g_failures++; \
      printf("FAIL %s:%d: %s == 0x%02X, expected 0x%02X\n", __FILE__, __LINE__, #actual, (actual), (expected)); \
    } \
  } while (0)

static std::vector<uint8_t> vec(std::initializer_list<uint8_t> l) { return std::vector<uint8_t>(l); }

static bool vec_eq(const std::vector<uint8_t> &a, std::initializer_list<uint8_t> b) {
  return a == std::vector<uint8_t>(b);
}

// ---------------------------------------------------------------- addresses

static void test_addresses() {
  CHECK(is_master_address(0x10));
  CHECK(is_master_address(0x31));
  CHECK(is_master_address(0xF1));
  CHECK(is_master_address(0x33));
  CHECK(is_master_address(0x00));
  CHECK(is_master_address(0xFF));
  CHECK(is_master_address(0x77));
  CHECK(!is_master_address(0x15));  // VRC720 slave address
  CHECK(!is_master_address(0x08));
  CHECK(!is_master_address(0x12));
  CHECK(!is_master_address(0x21));
  CHECK(!is_master_address(0xA9));
  CHECK(!is_master_address(0xAA));
  CHECK(!is_master_address(0xFE));  // broadcast
  CHECK(is_valid_address(0x15));
  CHECK(is_valid_address(0xFE));
  CHECK(!is_valid_address(0xFE, false));
  CHECK(!is_valid_address(0xA9));
  CHECK(!is_valid_address(0xAA));
}

// ---------------------------------------------------------------- CRC8

static void test_crc8() {
  // Known vector: CRC over "10feb5050427a915aa" is 0x77. Consistent with the
  // escape-expansion rule in protocols/ebus-services/ebus-overview.md: the
  // logical 0xA9 and 0xAA are expanded to (A9 00) / (A9 01) before the update.
  uint8_t data[] = {0x10, 0xFE, 0xB5, 0x05, 0x04, 0x27, 0xA9, 0x15, 0xAA};
  CHECK_EQ_HEX(crc8(data, sizeof(data)), 0x77);

  // CRC over logical bytes must equal CRC over the wire-escaped bytes.
  uint8_t logical[] = {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x0A, 0xAA, 0xA9};
  uint8_t wire[32];
  size_t wire_len = escape_bytes(logical, sizeof(logical), wire);
  uint8_t crc_wire = 0;
  for (size_t i = 0; i < wire_len; i++) {
    crc_wire = crc8_update(crc_wire, wire[i]);
  }
  CHECK_EQ_HEX(crc8(logical, sizeof(logical)), crc_wire);
  CHECK_EQ_HEX(crc8(logical, sizeof(logical)), 0x7A);

  // Incremental update matches one-shot computation.
  uint8_t crc = 0;
  for (uint8_t b : {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00}) {
    crc = crc8_update_logical(crc, b);
  }
  CHECK_EQ_HEX(crc, 0x20);
}

// ---------------------------------------------------------------- escaping

static void test_escape() {
  uint8_t out[16];
  size_t n = escape_bytes(vec({0xA9}).data(), 1, out);
  CHECK(n == 2 && out[0] == 0xA9 && out[1] == 0x00);
  n = escape_bytes(vec({0xAA}).data(), 1, out);
  CHECK(n == 2 && out[0] == 0xA9 && out[1] == 0x01);
  n = escape_bytes(vec({0x01, 0xA9, 0xAA, 0x02}).data(), 4, out);
  CHECK(n == 6 && out[0] == 0x01 && out[1] == 0xA9 && out[2] == 0x00 && out[3] == 0xA9 && out[4] == 0x01 &&
        out[5] == 0x02);
  n = escape_bytes(vec({0x00, 0xFF, 0x55}).data(), 3, out);
  CHECK(n == 3 && out[0] == 0x00 && out[1] == 0xFF && out[2] == 0x55);
}

// ---------------------------------------------------------------- builders

static void test_build_telegram() {
  uint8_t out[80];
  // B524 read of system register 0x0034 from 0x31 to 0x15.
  uint8_t data[] = {0x02, 0x00, 0x00, 0x34, 0x00};
  size_t n = build_telegram(0x31, 0x15, 0xB5, 0x24, data, sizeof(data), out);
  CHECK(vec_eq(std::vector<uint8_t>(out, out + n), {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20}));

  // Telegram whose payload contains 0xAA and 0xA9 (must be escaped on the wire).
  uint8_t data2[] = {0x02, 0x00, 0x0A, 0xAA, 0xA9};
  n = build_telegram(0x31, 0x15, 0xB5, 0x24, data2, sizeof(data2), out);
  CHECK(vec_eq(std::vector<uint8_t>(out, out + n),
               {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x0A, 0xA9, 0x01, 0xA9, 0x00, 0x7A}));

  // Identification request (LEN=0).
  n = build_telegram(0x31, 0x15, 0x07, 0x04, nullptr, 0, out);
  CHECK(vec_eq(std::vector<uint8_t>(out, out + n), {0x31, 0x15, 0x07, 0x04, 0x00, 0x8B}));

  // Broadcast inquiry of existence.
  n = build_telegram(0x31, 0xFE, 0x07, 0xFE, nullptr, 0, out);
  CHECK(vec_eq(std::vector<uint8_t>(out, out + n), {0x31, 0xFE, 0x07, 0xFE, 0x00, 0x35}));
}

static void test_build_response() {
  uint8_t out[80];
  uint8_t data[] = {0x03, 0x0A, 0x01, 0x00, 0x00};
  size_t n = build_response(data, sizeof(data), out);
  CHECK(vec_eq(std::vector<uint8_t>(out, out + n), {0x05, 0x03, 0x0A, 0x01, 0x00, 0x00, 0xBA}));

  // Response with escaped payload bytes.
  uint8_t data2[] = {0x01, 0xA9, 0xAA};
  n = build_response(data2, sizeof(data2), out);
  CHECK(vec_eq(std::vector<uint8_t>(out, out + n), {0x03, 0x01, 0xA9, 0x00, 0xA9, 0x01, 0xB1}));
}

// ---------------------------------------------------------------- decoder

struct DecoderEvents {
  std::vector<EBUSDecodedFrame> frames;
  std::vector<EBUSDecodedFrame> responses;
  std::vector<std::pair<uint8_t, uint8_t>> acks;
  std::vector<std::pair<uint8_t, uint8_t>> naks;
  int frame_crc_errors = 0;
  int response_crc_errors = 0;
  int framing_errors = 0;

  void attach(PassiveFrameDecoder &dec) {
    dec.on_frame = [this](const EBUSDecodedFrame &f) { this->frames.push_back(f); };
    dec.on_response = [this](const EBUSDecodedFrame &r) { this->responses.push_back(r); };
    dec.on_ack = [this](uint8_t from, uint8_t to) { this->acks.emplace_back(from, to); };
    dec.on_nak = [this](uint8_t from, uint8_t to) { this->naks.emplace_back(from, to); };
    dec.on_frame_crc_error = [this]() { this->frame_crc_errors++; };
    dec.on_response_crc_error = [this]() { this->response_crc_errors++; };
    dec.on_framing_error = [this]() { this->framing_errors++; };
  }

  void feed(PassiveFrameDecoder &dec, std::initializer_list<uint8_t> wire) {
    for (uint8_t b : wire) {
      dec.feed(b);
    }
  }
};

static bool frame_eq(const EBUSDecodedFrame &f, uint8_t src, uint8_t dst, uint8_t pb, uint8_t sb,
                     std::initializer_list<uint8_t> data) {
  if (f.src != src || f.dst != dst || f.pb != pb || f.sb != sb) {
    return false;
  }
  if (f.data_len != data.size()) {
    return false;
  }
  return memcmp(f.data, data.begin(), data.size()) == 0;
}

static void test_decoder_transaction() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // Full initiator/target transaction: request, ACK, response, ACK, SYN.
  ev.feed(dec, {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20,  // request
                0x00,                                                              // ACK from target
                0x05, 0x03, 0x0A, 0x01, 0x00, 0x00, 0xBA,                          // response
                0x00,                                                              // ACK from initiator
                0xAA});                                                            // SYN
  CHECK(ev.frames.size() == 1);
  CHECK(frame_eq(ev.frames[0], 0x31, 0x15, 0xB5, 0x24, {0x02, 0x00, 0x00, 0x34, 0x00}));
  CHECK(ev.responses.size() == 1);
  CHECK(frame_eq(ev.responses[0], 0x15, 0x31, 0xB5, 0x24, {0x03, 0x0A, 0x01, 0x00, 0x00}));
  CHECK(ev.acks.size() == 2);
  CHECK(ev.acks[0] == std::make_pair((uint8_t) 0x15, (uint8_t) 0x31));
  CHECK(ev.acks[1] == std::make_pair((uint8_t) 0x31, (uint8_t) 0x15));
  CHECK(ev.naks.empty());
  CHECK(ev.frame_crc_errors == 0 && ev.response_crc_errors == 0 && ev.framing_errors == 0);
}

static void test_decoder_broadcast() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  ev.feed(dec, {0x31, 0xFE, 0x07, 0xFE, 0x00, 0x35, 0xAA});
  CHECK(ev.frames.size() == 1);
  CHECK(frame_eq(ev.frames[0], 0x31, 0xFE, 0x07, 0xFE, {}));
  CHECK(ev.acks.empty());
  CHECK(ev.responses.empty());
}

static void test_decoder_i2i() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // Initiator-to-initiator: ACK only, no response phase.
  ev.feed(dec, {0x31, 0x10, 0x07, 0x04, 0x00, 0x36, 0x00, 0xAA});
  CHECK(ev.frames.size() == 1);
  CHECK(frame_eq(ev.frames[0], 0x31, 0x10, 0x07, 0x04, {}));
  CHECK(ev.acks.size() == 1);
  CHECK(ev.acks[0] == std::make_pair((uint8_t) 0x10, (uint8_t) 0x31));
  CHECK(ev.responses.empty());
}

static void test_decoder_nak_retransmit() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // Target NAKs the command; the initiator retransmits the whole telegram.
  ev.feed(dec, {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20, 0xFF,  // NAK
                0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20, 0x00,  // retransmit + ACK
                0xAA});
  CHECK(ev.frames.size() == 2);
  CHECK(ev.naks.size() == 1);
  CHECK(ev.naks[0] == std::make_pair((uint8_t) 0x15, (uint8_t) 0x31));
  CHECK(ev.acks.size() == 1);
}

static void test_decoder_response_nak_retransmit() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // Initiator NAKs the response; the target retransmits it.
  ev.feed(dec, {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20, 0x00,
                0x05, 0x03, 0x0A, 0x01, 0x00, 0x00, 0xBA, 0xFF,  // response + NAK
                0x05, 0x03, 0x0A, 0x01, 0x00, 0x00, 0xBA, 0x00,  // retransmit + ACK
                0xAA});
  CHECK(ev.responses.size() == 2);
  CHECK(frame_eq(ev.responses[0], 0x15, 0x31, 0xB5, 0x24, {0x03, 0x0A, 0x01, 0x00, 0x00}));
  CHECK(ev.naks.size() == 1);
  CHECK(ev.naks[0] == std::make_pair((uint8_t) 0x31, (uint8_t) 0x15));
  CHECK(ev.acks.size() == 2);
}

static void test_decoder_frame_crc_error() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // Bad command CRC: no frame reported, but the following NAK and the
  // retransmitted telegram are still tracked.
  ev.feed(dec, {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0xDF, 0xFF,
                0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20, 0x00, 0xAA});
  CHECK(ev.frames.size() == 1);  // only the retransmission
  CHECK(ev.frame_crc_errors == 1);
  CHECK(ev.naks.size() == 1);
  CHECK(ev.acks.size() == 1);
}

static void test_decoder_response_crc_error() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // Bad response CRC: no response reported, NAK + retransmission tracked.
  ev.feed(dec, {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20, 0x00,
                0x05, 0x03, 0x0A, 0x01, 0x00, 0x00, 0x45, 0xFF,  // bad CRC + NAK
                0x05, 0x03, 0x0A, 0x01, 0x00, 0x00, 0xBA, 0x00, 0xAA});
  CHECK(ev.responses.size() == 1);  // only the retransmission
  CHECK(ev.response_crc_errors == 1);
  CHECK(ev.naks.size() == 1);
}

static void test_decoder_escaped_payload() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // Request and response payloads containing 0xA9/0xAA (escaped on the wire).
  ev.feed(dec, {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x0A, 0xA9, 0x01, 0xA9, 0x00, 0x7A, 0x00,
                0x03, 0x01, 0xA9, 0x00, 0xA9, 0x01, 0xB1, 0x00, 0xAA});
  CHECK(ev.frames.size() == 1);
  CHECK(frame_eq(ev.frames[0], 0x31, 0x15, 0xB5, 0x24, {0x02, 0x00, 0x0A, 0xAA, 0xA9}));
  CHECK(ev.responses.size() == 1);
  CHECK(frame_eq(ev.responses[0], 0x15, 0x31, 0xB5, 0x24, {0x01, 0xA9, 0xAA}));
}

static void test_decoder_early_syn() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // SYN after a single collected byte = new arbitration cycle, not an error.
  ev.feed(dec, {0x31, 0xAA, 0x31, 0x15, 0x07, 0x04, 0x00, 0x8B, 0x00, 0xAA});
  CHECK(ev.frames.size() == 1);
  CHECK(frame_eq(ev.frames[0], 0x31, 0x15, 0x07, 0x04, {}));
  CHECK(ev.framing_errors == 0);
}

static void test_decoder_syn_during_wait() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // SYN while waiting for ACK = end of transaction (timeout indicator).
  // The first frame itself is complete and reported; its transaction is then
  // aborted (no ACK/response events), and the next frame is tracked normally.
  ev.feed(dec, {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20, 0xAA,
                0x31, 0x15, 0x07, 0x04, 0x00, 0x8B, 0x00, 0xAA});
  CHECK(ev.frames.size() == 2);
  CHECK(ev.acks.size() == 1);  // only from the second transaction
  CHECK(ev.responses.empty());
  CHECK(ev.framing_errors == 0);
}

static void test_decoder_syn_mid_frame() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // SYN in the middle of a frame: partial frame dropped, next frame tracked.
  ev.feed(dec, {0x31, 0x15, 0xB5, 0xAA, 0x31, 0x15, 0x07, 0x04, 0x00, 0x8B, 0x00, 0xAA});
  CHECK(ev.frames.size() == 1);
  CHECK(frame_eq(ev.frames[0], 0x31, 0x15, 0x07, 0x04, {}));
}

static void test_decoder_garbage_resync() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  // Garbage (non-master first byte) is skipped until the next SYN.
  ev.feed(dec, {0x15, 0x31, 0xB5, 0x24, 0xAA, 0x31, 0x15, 0x07, 0x04, 0x00, 0x8B, 0x00, 0xAA});
  CHECK(ev.frames.size() == 1);
  CHECK(frame_eq(ev.frames[0], 0x31, 0x15, 0x07, 0x04, {}));
}

static void test_decoder_two_transactions() {
  PassiveFrameDecoder dec;
  DecoderEvents ev;
  ev.attach(dec);
  ev.feed(dec, {0x31, 0x15, 0xB5, 0x24, 0x05, 0x02, 0x00, 0x00, 0x34, 0x00, 0x20, 0x00,
                0x05, 0x03, 0x0A, 0x01, 0x00, 0x00, 0xBA, 0x00, 0xAA,
                0x31, 0x15, 0x07, 0x04, 0x00, 0x8B, 0x00, 0xAA});
  CHECK(ev.frames.size() == 2);
  CHECK(ev.responses.size() == 1);
  CHECK(ev.acks.size() == 3);
}

int main() {
  test_addresses();
  test_crc8();
  test_escape();
  test_build_telegram();
  test_build_response();
  test_decoder_transaction();
  test_decoder_broadcast();
  test_decoder_i2i();
  test_decoder_nak_retransmit();
  test_decoder_response_nak_retransmit();
  test_decoder_frame_crc_error();
  test_decoder_response_crc_error();
  test_decoder_escaped_payload();
  test_decoder_early_syn();
  test_decoder_syn_during_wait();
  test_decoder_syn_mid_frame();
  test_decoder_garbage_resync();
  test_decoder_two_transactions();

  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

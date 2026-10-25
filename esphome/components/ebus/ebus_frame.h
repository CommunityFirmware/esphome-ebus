#pragma once

// eBUS frame codec (pure, no ESPHome dependencies).
//
// Wire format (direct mode):
//   initiator telegram: SRC DST PB SB LEN DATA... CRC
//   target response:    LEN DATA... CRC
// CRC8 (poly 0x9B, init 0x00) is computed over the wire-escaped bytes, i.e.
// over the logical bytes with 0xA9 -> (0xA9, 0x00) and 0xAA -> (0xA9, 0x01)
// expanded before each polynomial update.
//
// Reference: https://github.com/Project-Helianthus/helianthus-docs-ebus
//            (protocols/ebus-services/ebus-overview.md)

#include <cstddef>
#include <cstdint>
#include <functional>

#include "ebus_const.h"

namespace esphome {
namespace ebus {

// CRC8 update with one raw wire byte (table-driven, poly 0x9B, init 0x00).
uint8_t crc8_update(uint8_t crc, uint8_t wire_byte);

// CRC8 update with one logical byte; expands 0xA9/0xAA to their wire form first.
uint8_t crc8_update_logical(uint8_t crc, uint8_t logical_byte);

// CRC8 over a buffer of logical bytes.
uint8_t crc8(const uint8_t *data, size_t len);

// Escape logical bytes into wire form. `out` must have room for 2*in_len bytes.
// Returns the number of wire bytes written.
size_t escape_bytes(const uint8_t *in, size_t in_len, uint8_t *out);

// Build a full initiator telegram (wire form, including CRC).
// `out` must have room for 2*(6 + data_len) bytes. Returns the wire length.
size_t build_telegram(uint8_t src, uint8_t dst, uint8_t pb, uint8_t sb, const uint8_t *data, size_t data_len,
                      uint8_t *out);

// Build a target response (wire form: LEN DATA... CRC).
// `out` must have room for 2*(2 + data_len) bytes. Returns the wire length.
size_t build_response(const uint8_t *data, size_t data_len, uint8_t *out);

// A decoded frame with logical (unescaped) payload.
struct EBUSDecodedFrame {
  uint8_t src;
  uint8_t dst;
  uint8_t pb;
  uint8_t sb;
  uint8_t data[EBUS_MAX_DATA_LEN];
  size_t data_len;
};

// Passive bus monitor. Feed raw wire bytes (as received from the UART while this
// node is not transmitting); complete, CRC-valid frames are reported through
// the callbacks. The decoder tracks the ACK/NAK handshake so that request and
// response frames stay correlated.
class PassiveFrameDecoder {
 public:
  void reset();
  void feed(uint8_t wire_byte);

  // A complete request frame with valid CRC was seen on the bus.
  std::function<void(const EBUSDecodedFrame &frame)> on_frame;
  // A complete response frame with valid CRC was seen (src/dst/pb/sb are
  // inferred from the request it answers).
  std::function<void(const EBUSDecodedFrame &response)> on_response;
  // ACK/NAK observed on the bus; arguments are (sender, receiver).
  std::function<void(uint8_t from, uint8_t to)> on_ack;
  std::function<void(uint8_t from, uint8_t to)> on_nak;
  // A frame/response failed CRC verification (it is not reported via on_frame).
  std::function<void()> on_frame_crc_error;
  std::function<void()> on_response_crc_error;
  // Protocol violation (bad escape sequence, oversized LEN, unexpected byte).
  std::function<void()> on_framing_error;

 protected:
  enum class State : uint8_t {
    WAIT_FRAME,          // collecting SRC DST PB SB LEN DATA...
    WAIT_FRAME_CRC,      // collecting the CRC byte of a request frame
    EXPECT_ACK,          // waiting for the target's ACK/NAK
    EXPECT_RESPONSE,     // collecting LEN DATA... of the response
    WAIT_RESPONSE_CRC,   // collecting the CRC byte of the response
    EXPECT_RESPONSE_ACK, // waiting for the initiator's ACK/NAK of the response
    SKIP,                // discard bytes until the next SYN (error recovery)
  };

  void handle_syn_();
  void handle_byte_(uint8_t logical_byte);
  void finish_frame_();
  void finish_response_();

  State state_{State::WAIT_FRAME};
  bool escape_pending_{false};

  // request frame collection (logical bytes: SRC DST PB SB LEN DATA...)
  uint8_t frame_buf_[5 + EBUS_MAX_DATA_LEN];
  size_t frame_len_{0};
  uint8_t frame_crc_{0};

  // header of the request currently being tracked (for ACK/response correlation)
  uint8_t req_src_{0};
  uint8_t req_dst_{0};
  uint8_t req_pb_{0};
  uint8_t req_sb_{0};

  // response collection (logical bytes: LEN DATA...)
  uint8_t resp_buf_[1 + EBUS_MAX_DATA_LEN];
  size_t resp_len_{0};
  uint8_t resp_crc_{0};
};

}  // namespace ebus
}  // namespace esphome

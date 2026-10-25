#include "ebus_frame.h"

#include <cstring>

namespace esphome {
namespace ebus {

// CRC8 lookup table (poly 0x9B, init 0x00). Update rule: crc = TABLE[crc] ^ byte.
// The CRC is computed over the wire (escape-expanded) form of the frame, per
// protocols/ebus-services/ebus-overview.md, section "CRC8 and Escaping".
static const uint8_t CRC8_TABLE[256] = {
  0x00, 0x9B, 0xAD, 0x36, 0xC1, 0x5A, 0x6C, 0xF7, 0x19, 0x82, 0xB4, 0x2F, 0xD8, 0x43, 0x75, 0xEE,
  0x32, 0xA9, 0x9F, 0x04, 0xF3, 0x68, 0x5E, 0xC5, 0x2B, 0xB0, 0x86, 0x1D, 0xEA, 0x71, 0x47, 0xDC,
  0x64, 0xFF, 0xC9, 0x52, 0xA5, 0x3E, 0x08, 0x93, 0x7D, 0xE6, 0xD0, 0x4B, 0xBC, 0x27, 0x11, 0x8A,
  0x56, 0xCD, 0xFB, 0x60, 0x97, 0x0C, 0x3A, 0xA1, 0x4F, 0xD4, 0xE2, 0x79, 0x8E, 0x15, 0x23, 0xB8,
  0xC8, 0x53, 0x65, 0xFE, 0x09, 0x92, 0xA4, 0x3F, 0xD1, 0x4A, 0x7C, 0xE7, 0x10, 0x8B, 0xBD, 0x26,
  0xFA, 0x61, 0x57, 0xCC, 0x3B, 0xA0, 0x96, 0x0D, 0xE3, 0x78, 0x4E, 0xD5, 0x22, 0xB9, 0x8F, 0x14,
  0xAC, 0x37, 0x01, 0x9A, 0x6D, 0xF6, 0xC0, 0x5B, 0xB5, 0x2E, 0x18, 0x83, 0x74, 0xEF, 0xD9, 0x42,
  0x9E, 0x05, 0x33, 0xA8, 0x5F, 0xC4, 0xF2, 0x69, 0x87, 0x1C, 0x2A, 0xB1, 0x46, 0xDD, 0xEB, 0x70,
  0x0B, 0x90, 0xA6, 0x3D, 0xCA, 0x51, 0x67, 0xFC, 0x12, 0x89, 0xBF, 0x24, 0xD3, 0x48, 0x7E, 0xE5,
  0x39, 0xA2, 0x94, 0x0F, 0xF8, 0x63, 0x55, 0xCE, 0x20, 0xBB, 0x8D, 0x16, 0xE1, 0x7A, 0x4C, 0xD7,
  0x6F, 0xF4, 0xC2, 0x59, 0xAE, 0x35, 0x03, 0x98, 0x76, 0xED, 0xDB, 0x40, 0xB7, 0x2C, 0x1A, 0x81,
  0x5D, 0xC6, 0xF0, 0x6B, 0x9C, 0x07, 0x31, 0xAA, 0x44, 0xDF, 0xE9, 0x72, 0x85, 0x1E, 0x28, 0xB3,
  0xC3, 0x58, 0x6E, 0xF5, 0x02, 0x99, 0xAF, 0x34, 0xDA, 0x41, 0x77, 0xEC, 0x1B, 0x80, 0xB6, 0x2D,
  0xF1, 0x6A, 0x5C, 0xC7, 0x30, 0xAB, 0x9D, 0x06, 0xE8, 0x73, 0x45, 0xDE, 0x29, 0xB2, 0x84, 0x1F,
  0xA7, 0x3C, 0x0A, 0x91, 0x66, 0xFD, 0xCB, 0x50, 0xBE, 0x25, 0x13, 0x88, 0x7F, 0xE4, 0xD2, 0x49,
  0x95, 0x0E, 0x38, 0xA3, 0x54, 0xCF, 0xF9, 0x62, 0x8C, 0x17, 0x21, 0xBA, 0x4D, 0xD6, 0xE0, 0x7B,
};

uint8_t crc8_update(uint8_t crc, uint8_t wire_byte) { return CRC8_TABLE[crc] ^ wire_byte; }

uint8_t crc8_update_logical(uint8_t crc, uint8_t logical_byte) {
  if (logical_byte == EBUS_ESC) {
    return crc8_update(crc8_update(crc, EBUS_ESC), EBUS_ESC_ESC);
  }
  if (logical_byte == EBUS_SYN) {
    return crc8_update(crc8_update(crc, EBUS_ESC), EBUS_ESC_SYN);
  }
  return crc8_update(crc, logical_byte);
}

uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc = crc8_update_logical(crc, data[i]);
  }
  return crc;
}

size_t escape_bytes(const uint8_t *in, size_t in_len, uint8_t *out) {
  size_t n = 0;
  for (size_t i = 0; i < in_len; i++) {
    if (in[i] == EBUS_ESC) {
      out[n++] = EBUS_ESC;
      out[n++] = EBUS_ESC_ESC;
    } else if (in[i] == EBUS_SYN) {
      out[n++] = EBUS_ESC;
      out[n++] = EBUS_ESC_SYN;
    } else {
      out[n++] = in[i];
    }
  }
  return n;
}

size_t build_telegram(uint8_t src, uint8_t dst, uint8_t pb, uint8_t sb, const uint8_t *data, size_t data_len,
                      uint8_t *out) {
  uint8_t logical[6 + EBUS_MAX_DATA_LEN];
  logical[0] = src;
  logical[1] = dst;
  logical[2] = pb;
  logical[3] = sb;
  logical[4] = static_cast<uint8_t>(data_len);
  if (data_len > 0) {
    memcpy(&logical[5], data, data_len);
  }
  logical[5 + data_len] = crc8(logical, 5 + data_len);
  return escape_bytes(logical, 6 + data_len, out);
}

size_t build_response(const uint8_t *data, size_t data_len, uint8_t *out) {
  uint8_t logical[2 + EBUS_MAX_DATA_LEN];
  logical[0] = static_cast<uint8_t>(data_len);
  if (data_len > 0) {
    memcpy(&logical[1], data, data_len);
  }
  logical[1 + data_len] = crc8(logical, 1 + data_len);
  return escape_bytes(logical, 2 + data_len, out);
}

void PassiveFrameDecoder::reset() {
  this->state_ = State::WAIT_FRAME;
  this->escape_pending_ = false;
  this->frame_len_ = 0;
  this->frame_crc_ = 0;
  this->req_src_ = 0;
  this->req_dst_ = 0;
  this->req_pb_ = 0;
  this->req_sb_ = 0;
  this->resp_len_ = 0;
  this->resp_crc_ = 0;
}

void PassiveFrameDecoder::feed(uint8_t wire_byte) {
  if (this->state_ == State::SKIP) {
    if (wire_byte == EBUS_SYN) {
      this->state_ = State::WAIT_FRAME;
    }
    return;
  }
  if (this->escape_pending_) {
    this->escape_pending_ = false;
    if (wire_byte == EBUS_ESC_ESC) {
      this->handle_byte_(EBUS_ESC);
      return;
    }
    if (wire_byte == EBUS_ESC_SYN) {
      this->handle_byte_(EBUS_SYN);
      return;
    }
    // invalid escape sequence
    if (this->on_framing_error) {
      this->on_framing_error();
    }
    this->state_ = State::SKIP;
    return;
  }
  if (wire_byte == EBUS_ESC) {
    this->escape_pending_ = true;
    return;
  }
  if (wire_byte == EBUS_SYN) {
    this->handle_syn_();
    return;
  }
  this->handle_byte_(wire_byte);
}

void PassiveFrameDecoder::handle_syn_() {
  switch (this->state_) {
    case State::WAIT_FRAME:
      // Idle marker, or an early SYN (at most one collected byte) which starts
      // a new arbitration cycle: drop any partial frame and keep listening.
      this->frame_len_ = 0;
      this->frame_crc_ = 0;
      break;
    case State::WAIT_FRAME_CRC:
      // SYN instead of the CRC byte: framing error.
      if (this->on_framing_error) {
        this->on_framing_error();
      }
      this->state_ = State::WAIT_FRAME;
      this->frame_len_ = 0;
      this->frame_crc_ = 0;
      break;
    case State::EXPECT_ACK:
    case State::EXPECT_RESPONSE:
    case State::WAIT_RESPONSE_CRC:
    case State::EXPECT_RESPONSE_ACK:
      // A SYN during an active wait marks the end of the transaction (timeout).
      this->state_ = State::WAIT_FRAME;
      this->frame_len_ = 0;
      this->frame_crc_ = 0;
      this->resp_len_ = 0;
      this->resp_crc_ = 0;
      break;
    case State::SKIP:
      break;
  }
}

void PassiveFrameDecoder::handle_byte_(uint8_t b) {
  switch (this->state_) {
    case State::WAIT_FRAME: {
      if (this->frame_len_ == 0) {
        if (!is_master_address(b)) {
          // Not the start of an initiator telegram we can track.
          this->state_ = State::SKIP;
          return;
        }
        this->frame_crc_ = 0;
      }
      if (this->frame_len_ >= sizeof(this->frame_buf_)) {
        if (this->on_framing_error) {
          this->on_framing_error();
        }
        this->state_ = State::SKIP;
        return;
      }
      this->frame_buf_[this->frame_len_++] = b;
      this->frame_crc_ = crc8_update_logical(this->frame_crc_, b);
      if (this->frame_len_ == 5 && this->frame_buf_[4] > EBUS_MAX_DATA_LEN) {
        if (this->on_framing_error) {
          this->on_framing_error();
        }
        this->state_ = State::SKIP;
        return;
      }
      if (this->frame_len_ >= 5 && this->frame_len_ >= 5 + this->frame_buf_[4]) {
        this->state_ = State::WAIT_FRAME_CRC;
      }
      break;
    }

    case State::WAIT_FRAME_CRC: {
      uint8_t dst = this->frame_buf_[1];
      if (b == this->frame_crc_) {
        this->finish_frame_();
      } else {
        if (this->on_frame_crc_error) {
          this->on_frame_crc_error();
        }
        // Remember the header so the following ACK/NAK can still be attributed.
        this->req_src_ = this->frame_buf_[0];
        this->req_dst_ = dst;
        this->req_pb_ = this->frame_buf_[2];
        this->req_sb_ = this->frame_buf_[3];
      }
      this->frame_len_ = 0;
      this->frame_crc_ = 0;
      // Broadcast frames are not acknowledged; everything else is followed by
      // an ACK/NAK byte (even when the CRC failed: the target NAKs and the
      // initiator retransmits, which we then pick up as a new frame).
      this->state_ = (dst == EBUS_BROADCAST) ? State::WAIT_FRAME : State::EXPECT_ACK;
      break;
    }

    case State::EXPECT_ACK: {
      if (b == EBUS_ACK) {
        if (this->on_ack) {
          this->on_ack(this->req_dst_, this->req_src_);
        }
        if (is_master_address(this->req_dst_)) {
          // initiator-to-initiator: ACK only, no response phase
          this->state_ = State::WAIT_FRAME;
        } else {
          this->state_ = State::EXPECT_RESPONSE;
          this->resp_len_ = 0;
          this->resp_crc_ = 0;
        }
      } else if (b == EBUS_NAK) {
        if (this->on_nak) {
          this->on_nak(this->req_dst_, this->req_src_);
        }
        // the initiator retransmits the whole telegram
        this->state_ = State::WAIT_FRAME;
      } else {
        if (this->on_framing_error) {
          this->on_framing_error();
        }
        this->state_ = State::SKIP;
      }
      break;
    }

    case State::EXPECT_RESPONSE: {
      if (this->resp_len_ >= sizeof(this->resp_buf_)) {
        if (this->on_framing_error) {
          this->on_framing_error();
        }
        this->state_ = State::SKIP;
        return;
      }
      this->resp_buf_[this->resp_len_++] = b;
      this->resp_crc_ = crc8_update_logical(this->resp_crc_, b);
      if (this->resp_len_ == 1 && this->resp_buf_[0] > EBUS_MAX_DATA_LEN) {
        if (this->on_framing_error) {
          this->on_framing_error();
        }
        this->state_ = State::SKIP;
        return;
      }
      if (this->resp_len_ >= 1 && this->resp_len_ >= 1 + this->resp_buf_[0]) {
        this->state_ = State::WAIT_RESPONSE_CRC;
      }
      break;
    }

    case State::WAIT_RESPONSE_CRC: {
      if (b == this->resp_crc_) {
        this->finish_response_();
      } else {
        if (this->on_response_crc_error) {
          this->on_response_crc_error();
        }
      }
      this->resp_len_ = 0;
      this->resp_crc_ = 0;
      // The initiator ACKs or NAKs the response either way; on NAK the target
      // retransmits, which we then pick up as a new response.
      this->state_ = State::EXPECT_RESPONSE_ACK;
      break;
    }

    case State::EXPECT_RESPONSE_ACK: {
      if (b == EBUS_ACK) {
        if (this->on_ack) {
          this->on_ack(this->req_src_, this->req_dst_);
        }
        this->state_ = State::WAIT_FRAME;
      } else if (b == EBUS_NAK) {
        if (this->on_nak) {
          this->on_nak(this->req_src_, this->req_dst_);
        }
        this->state_ = State::EXPECT_RESPONSE;
        this->resp_len_ = 0;
        this->resp_crc_ = 0;
      } else {
        if (this->on_framing_error) {
          this->on_framing_error();
        }
        this->state_ = State::SKIP;
      }
      break;
    }

    case State::SKIP:
      break;
  }
}

void PassiveFrameDecoder::finish_frame_() {
  EBUSDecodedFrame frame;
  frame.src = this->frame_buf_[0];
  frame.dst = this->frame_buf_[1];
  frame.pb = this->frame_buf_[2];
  frame.sb = this->frame_buf_[3];
  frame.data_len = this->frame_buf_[4];
  if (frame.data_len > 0) {
    memcpy(frame.data, &this->frame_buf_[5], frame.data_len);
  }
  this->req_src_ = frame.src;
  this->req_dst_ = frame.dst;
  this->req_pb_ = frame.pb;
  this->req_sb_ = frame.sb;
  if (this->on_frame) {
    this->on_frame(frame);
  }
}

void PassiveFrameDecoder::finish_response_() {
  EBUSDecodedFrame response;
  response.src = this->req_dst_;  // the responder
  response.dst = this->req_src_;  // the original initiator
  response.pb = this->req_pb_;
  response.sb = this->req_sb_;
  response.data_len = this->resp_buf_[0];
  if (response.data_len > 0) {
    memcpy(response.data, &this->resp_buf_[1], response.data_len);
  }
  if (this->on_response) {
    this->on_response(response);
  }
}

}  // namespace ebus
}  // namespace esphome

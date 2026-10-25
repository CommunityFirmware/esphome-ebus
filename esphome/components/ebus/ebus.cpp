#include "ebus.h"

#include <algorithm>
#include <string>

#include "esphome/core/alloc_helpers.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "driver/gpio.h"
// eBUS Adapter Shield C6: eBUS RX/TX are hardwired to GPIO14/GPIO15.
static const gpio_num_t EBUS_RX_PIN = GPIO_NUM_14;
static const gpio_num_t EBUS_TX_PIN = GPIO_NUM_15;

#if defined(CONFIG_IDF_TARGET_ESP32C6)
#include "esp_private/gpio.h"
#include "soc/io_mux_reg.h"
#endif
#endif

namespace esphome {
namespace ebus {

static const char *const TAG = "ebus";

void EBUSHub::setup() {
  // Drop any garbage received before setup completed.
  while (this->available() > 0) {
    uint8_t byte;
    if (!this->read_byte(&byte)) {
      break;
    }
  }
  this->last_rx_ms_ = millis();
  if (this->listen_only_) {
    ESP_LOGW(TAG, "listen-only mode: requests are queued but never transmitted");
  }
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32C6)
  // ESP-IDF's uart_set_pin() enables the input for the RX pin but never sets
  // its IOMUX function to GPIO. On the ESP32-C6 the GPIO function is IOMUX
  // index 1 (not 0 as on the classic ESP32), so a pin left at the reset
  // default MCU_SEL=0 is NOT in GPIO mode. Force both eBUS pins to GPIO
  // function so the GPIO-matrix routing of the UART actually reaches the pads.
  gpio_func_sel(EBUS_RX_PIN, PIN_FUNC_GPIO);
  gpio_func_sel(EBUS_TX_PIN, PIN_FUNC_GPIO);
  // The transceiver RX output may be open-collector and rely on a pull-up.
  // Enable the internal pull-up on the eBUS RX pin; harmless with a
  // push-pull driver, and it makes an open-collector output readable.
  // Purely passive (input pin).
  gpio_set_pull_mode(EBUS_RX_PIN, GPIO_PULLUP_ONLY);
#endif

  this->decoder_.on_frame = [this](const EBUSDecodedFrame &frame) {
    this->stat_frames_seen_++;
    char hex[3 * EBUS_MAX_DATA_LEN];
    ESP_LOGD(TAG, "passive frame: 0x%02X -> 0x%02X PB 0x%02X SB 0x%02X NN %u: %s", frame.src, frame.dst, frame.pb,
             frame.sb, (unsigned) frame.data_len,
             format_hex_pretty_to(hex, frame.data, frame.data_len, ' '));
    for (auto &cb : this->frame_callbacks_) {
      cb(frame);
    }
  };
  this->decoder_.on_response = [this](const EBUSDecodedFrame &response) {
    this->stat_responses_seen_++;
    char hex[3 * EBUS_MAX_DATA_LEN];
    ESP_LOGD(TAG, "passive response: 0x%02X -> 0x%02X PB 0x%02X SB 0x%02X NN %u: %s", response.src, response.dst,
             response.pb, response.sb, (unsigned) response.data_len,
             format_hex_pretty_to(hex, response.data, response.data_len, ' '));
    for (auto &cb : this->response_callbacks_) {
      cb(response);
    }
  };
  this->decoder_.on_frame_crc_error = [this]() { this->stat_crc_errors_++; };
  this->decoder_.on_response_crc_error = [this]() { this->stat_crc_errors_++; };
}

void EBUSHub::dump_config() {
  ESP_LOGCONFIG(TAG, "eBUS hub:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
  ESP_LOGCONFIG(TAG, "  Arbitration delay: %u us", (unsigned) this->arbitration_delay_us_);
  ESP_LOGCONFIG(TAG, "  Transactions: %u ok, %u failed (%u bus lost, %u CRC errors)", this->stat_success_,
                this->stat_failed_, this->stat_bus_lost_, this->stat_crc_errors_);
  ESP_LOGCONFIG(TAG, "  Passive: %u frames, %u responses seen", this->stat_frames_seen_,
                this->stat_responses_seen_);
  ESP_LOGCONFIG(TAG, "  RX bytes: %u", this->stat_rx_bytes_);
}

bool EBUSHub::send_request(uint8_t dst, uint8_t pb, uint8_t sb, const std::vector<uint8_t> &data,
                           EBUSRequestCallback &&callback) {
  // Address-position rules (protocols/ebus-services/ebus-overview.md): 0xA9
  // (escape) and 0xAA (SYN) are invalid in address positions; a telegram must
  // not be self-addressed. The broadcast address is a legal destination.
  if (!is_valid_address(dst) || dst == this->address_) {
    ESP_LOGE(TAG, "refusing request to invalid destination 0x%02X", dst);
    return false;
  }
  if (data.size() > EBUS_MAX_DATA_LEN) {
    ESP_LOGE(TAG, "refusing request to 0x%02X: %u data bytes exceeds the %u maximum", dst,
             (unsigned) data.size(), (unsigned) EBUS_MAX_DATA_LEN);
    return false;
  }
  if (this->queue_.size() >= MAX_QUEUE_LEN) {
    ESP_LOGW(TAG, "request queue full, dropping request to 0x%02X", dst);
    return false;
  }
  PendingRequest request;
  request.dst = dst;
  request.pb = pb;
  request.sb = sb;
  request.data_len = data.size();
  std::copy(data.begin(), data.end(), request.data);
  request.callback = std::move(callback);
  this->queue_.push(request);
  return true;
}

bool EBUSHub::send_broadcast(uint8_t pb, uint8_t sb, const std::vector<uint8_t> &data) {
  return this->send_request(EBUS_BROADCAST, pb, sb, data, nullptr);
}

void EBUSHub::add_on_frame_callback(std::function<void(const EBUSDecodedFrame &)> &&callback) {
  this->frame_callbacks_.push_back(std::move(callback));
}

void EBUSHub::add_on_response_callback(std::function<void(const EBUSDecodedFrame &)> &&callback) {
  this->response_callbacks_.push_back(std::move(callback));
}

void EBUSHub::loop() {
  // All loop-context timestamps use the loop start time, so send/receive
  // stamps and timeout checks share one monotonic base.
  uint32_t now = App.get_loop_component_start_time();

  // Guard with available(): read_byte() busy-waits (and logs an error) when
  // no data is pending, which would stall the main loop while the bus is idle.
  while (this->available() > 0) {
    uint8_t byte;
    if (!this->read_byte(&byte)) {
      break;
    }
    this->stat_rx_bytes_++;
    this->last_rx_ms_ = now;
    this->own_bus_free_ = false;
    if (this->state_ == State::IDLE) {
      if (byte == EBUS_SYN) {
        // A standalone SYN marks the bus as free (AUTO-SYN generator or
        // end-of-message); arbitration starts right after it. Count down the
        // arbitration lockout (ebusd reference semantics, see ebus_const.h).
        this->last_rx_was_syn_ = true;
        if (this->syn_lockout_ > 0) {
          this->syn_lockout_--;
        }
      } else {
        this->last_rx_was_syn_ = false;
      }
      this->decoder_.feed(byte);
    } else {
      this->handle_rx_byte_(byte, now);
    }
  }

  if (this->state_ == State::IDLE) {
    // A silent bus-idle period also counts as a bus-free phase, so the
    // lockout cannot stall forever on a quiet bus. Stamping last_rx_ms_
    // makes each phase a full EBUS_BUS_IDLE_MS long.
    if (this->syn_lockout_ > 0 && now - this->last_rx_ms_ >= EBUS_BUS_IDLE_MS) {
      this->syn_lockout_--;
      this->last_rx_ms_ = now;
    }
    if (!this->listen_only_ && (!this->queue_.empty() || this->current_valid_) && this->bus_idle_(now)) {
      // Arbitration delay (see ebus_const.h): yield the first microseconds of
      // the bus-free phase to competing initiators instead of colliding with
      // their start bit. The window opens on the first observation of the
      // free bus; RX activity during it makes bus_idle_() false again and
      // restarts the window on the next iteration. micros() (not the loop
      // start time) is required here: the delay is shorter than one loop
      // iteration, so millisecond resolution cannot observe its elapse.
      uint32_t now_us = micros();
      if (!this->bus_free_stamped_) {
        this->bus_free_stamped_ = true;
        this->bus_free_at_us_ = now_us;
      }
      if (now_us - this->bus_free_at_us_ >= this->arbitration_delay_us_) {
        this->bus_free_stamped_ = false;
        this->start_transaction_();
      }
    } else {
      this->bus_free_stamped_ = false;
    }
  } else {
    this->check_timeouts_(now);
  }

#ifdef USE_LIGHT
  this->update_status_led_(now);
#endif
}

bool EBUSHub::bus_idle_(uint32_t now) const {
  // Late-answer drain (see EBUS_DRAIN_MS): after a receive timeout the target
  // may still transmit its answer; starting a telegram now would collide with
  // it. This gate also defers the own_bus_free_ shortcut below.
  if (now < this->drain_until_ms_) {
    return false;
  }
  if (this->syn_lockout_ > 0) {
    return false;
  }
  if (this->own_bus_free_) {
    return true;
  }
  // A bus with an AUTO-SYN generator never falls silent for EBUS_BUS_IDLE_MS:
  // the generator injects a SYN every ~40ms while the bus is free. Per the
  // eBUS protocol, the bus is free ON the SYN symbol, so the last received
  // byte being a standalone SYN means arbitration may start immediately.
  if (this->last_rx_was_syn_) {
    return true;
  }
  return now - this->last_rx_ms_ >= EBUS_BUS_IDLE_MS;
}

void EBUSHub::start_transaction_() {
  if (!this->current_valid_) {
    this->current_ = std::move(this->queue_.front());
    this->queue_.pop();
    this->current_valid_ = true;
    this->current_.wire_len = build_telegram(this->address_, this->current_.dst, this->current_.pb, this->current_.sb,
                                              this->current_.data, this->current_.data_len, this->current_.wire);
    char hex[3 * sizeof(PendingRequest::wire)];
    ESP_LOGD(TAG, "send telegram to 0x%02X: %s", this->current_.dst,
             format_hex_pretty_to(hex, this->current_.wire, this->current_.wire_len, ' '));
    this->current_.nak_repeat = false;
    this->current_.resp_nak_repeat = false;
  }
  // current_ is already prepared: a retry after a lost arbitration resumes
  // here without re-queueing (StaticRingBuffer has no push_front).
  this->send_pos_ = 0;
  this->send_current_byte_();
}

void EBUSHub::send_current_byte_() {
  uint8_t b = this->current_.wire[this->send_pos_];
  this->write_byte(b);
  this->sent_ms_ = App.get_loop_component_start_time();
  this->state_ = State::SEND_BYTE;
}

void EBUSHub::send_syn_() {
  this->write_byte(EBUS_SYN);
  this->sent_ms_ = App.get_loop_component_start_time();
  this->state_ = State::SEND_SYN;
}

void EBUSHub::finish_transaction_(bool success, const EBUSResponse &response) {
  if (success) {
    this->stat_success_++;
  } else {
    this->stat_failed_++;
  }
  auto callback = std::move(this->current_.callback);
  this->current_valid_ = false;
  if (callback) {
    callback(success, response);
  }
  this->send_syn_();
}

void EBUSHub::fail_transaction_(const char *reason, bool release_bus) {
  if (this->current_valid_) {
    ESP_LOGD(TAG, "transaction to 0x%02X failed: %s", this->current_.dst, reason);
    auto callback = std::move(this->current_.callback);
    this->current_valid_ = false;
    if (callback) {
      callback(false, EBUSResponse{});
    }
  }
  this->stat_failed_++;
  if (release_bus) {
    this->send_syn_();
  } else {
    this->state_ = State::IDLE;
  }
}

void EBUSHub::handle_bus_lost_(const char *reason, const uint8_t *competing_byte) {
  this->stat_bus_lost_++;
  if (competing_byte != nullptr) {
    // Arbitration lockout (ebusd reference semantics, see ebus_const.h): the
    // colliding symbol is the competitor's address byte. Same priority class
    // (equal low nibble) locks out for 2 bus-free phases, a different class
    // for 3, and a non-initiator symbol for 1.
    uint8_t lockout;
    if (!is_master_address(*competing_byte)) {
      lockout = EBUS_LOCKOUT_MIN;
    } else if ((*competing_byte & 0x0F) != (this->address_ & 0x0F)) {
      lockout = EBUS_LOCKOUT_DIFFERENT_PRIORITY;
    } else {
      lockout = EBUS_LOCKOUT_SAME_PRIORITY;
    }
    this->syn_lockout_ = lockout;
  }
  if (this->current_valid_ && this->current_.bus_lost_retries < EBUS_ACQUIRE_RETRIES) {
    this->current_.bus_lost_retries++;
    ESP_LOGD(TAG, "bus lost during send to 0x%02X (%s), retry %u/%u", this->current_.dst, reason,
             this->current_.bus_lost_retries, EBUS_ACQUIRE_RETRIES);
    // Keep current_ valid: the request is retried in place once the bus is
    // free again (start_transaction_ resumes it without re-queueing).
    this->state_ = State::IDLE;
  } else {
    // Out of retries: fail without touching the bus (another master owns it).
    this->fail_transaction_("arbitration lost", false);
  }
}

void EBUSHub::handle_rx_byte_(uint8_t byte, uint32_t now) {
  switch (this->state_) {
    case State::SEND_BYTE: {
      // Per-byte echo check; a mismatch means another master is driving the bus.
      if (byte != this->current_.wire[this->send_pos_]) {
        ESP_LOGD(TAG, "echo mismatch: sent 0x%02X, received 0x%02X", this->current_.wire[this->send_pos_], byte);
        this->handle_bus_lost_("echo mismatch", &byte);
        return;
      }
      this->send_pos_++;
      if (this->send_pos_ < this->current_.wire_len) {
        this->send_current_byte_();
      } else if (this->current_.dst == EBUS_BROADCAST) {
        // Broadcast: no ACK/response, end with SYN.
        this->finish_transaction_(true, EBUSResponse{});
      } else {
        this->state_ = State::WAIT_ACK;
        this->wait_start_ms_ = now;
      }
      break;
    }

    case State::WAIT_ACK: {
      if (byte == EBUS_ACK) {
        if (is_master_address(this->current_.dst)) {
          // initiator-to-initiator: ACK only, no response phase.
          this->finish_transaction_(true, EBUSResponse{});
        } else {
          this->state_ = State::RECV_RESPONSE;
          this->resp_len_ = 0;
          this->resp_crc_ = 0;
          this->escape_pending_ = false;
          this->wait_start_ms_ = now;
        }
      } else if (byte == EBUS_NAK) {
        if (!this->current_.nak_repeat) {
          // Retry the whole telegram once without re-arbitration (eBUS spec SS7.4).
          this->current_.nak_repeat = true;
          this->send_pos_ = 0;
          this->send_current_byte_();
        } else {
          this->fail_transaction_("NAK", true);
        }
      } else if (byte == EBUS_SYN) {
        // SYN during an active wait marks the end of the transaction (timeout).
        this->fail_transaction_("SYN while waiting for ACK", true);
      } else {
        this->fail_transaction_("unexpected byte while waiting for ACK", true);
      }
      break;
    }

    case State::RECV_RESPONSE:
    case State::RECV_RESPONSE_CRC: {
      uint8_t logical;
      if (this->escape_pending_) {
        this->escape_pending_ = false;
        if (byte == EBUS_ESC_ESC) {
          logical = EBUS_ESC;
        } else if (byte == EBUS_ESC_SYN) {
          logical = EBUS_SYN;
        } else {
          this->fail_transaction_("invalid escape sequence in response", true);
          return;
        }
      } else if (byte == EBUS_ESC) {
        this->escape_pending_ = true;
        // The escape prefix is part of the wire form of a LEN/DATA byte and
        // counts towards the CRC; a prefix in front of the CRC byte does not.
        if (this->state_ == State::RECV_RESPONSE) {
          this->resp_crc_ = crc8_update(this->resp_crc_, byte);
        }
        return;
      } else if (byte == EBUS_SYN) {
        this->fail_transaction_("SYN while receiving response", true);
        return;
      } else {
        logical = byte;
      }

      if (this->state_ == State::RECV_RESPONSE) {
        // The CRC is computed over the raw wire bytes of LEN DATA... only;
        // the CRC byte itself is compared against it, never fed into it.
        this->resp_crc_ = crc8_update(this->resp_crc_, byte);
        if (this->resp_len_ >= sizeof(this->resp_buf_)) {
          this->fail_transaction_("response too long", true);
          return;
        }
        this->resp_buf_[this->resp_len_++] = logical;
        if (this->resp_len_ == 1 && this->resp_buf_[0] > EBUS_MAX_DATA_LEN) {
          this->fail_transaction_("response length too large", true);
          return;
        }
        if (this->resp_len_ >= 1 && this->resp_len_ >= 1 + this->resp_buf_[0]) {
          this->state_ = State::RECV_RESPONSE_CRC;
        } else {
          this->wait_start_ms_ = now;  // refresh the per-byte timeout
        }
      } else {  // RECV_RESPONSE_CRC
        if (logical == this->resp_crc_) {
          this->ack_byte_ = EBUS_ACK;
        } else {
          this->stat_crc_errors_++;
          if (!this->current_.resp_nak_repeat) {
            // Ask the target to retransmit the response once.
            this->current_.resp_nak_repeat = true;
            this->ack_byte_ = EBUS_NAK;
          } else {
            this->fail_transaction_("response CRC error", true);
            return;
          }
        }
        this->write_byte(this->ack_byte_);
        this->sent_ms_ = now;
        this->state_ = State::SEND_ACK;
      }
      break;
    }

    case State::SEND_ACK: {
      if (byte != this->ack_byte_) {
        this->fail_transaction_("ACK echo mismatch", false);
        return;
      }
      if (this->ack_byte_ == EBUS_ACK) {
        EBUSResponse response;
        response.src = this->current_.dst;
        response.dst = this->address_;
        response.pb = this->current_.pb;
        response.sb = this->current_.sb;
        if (this->resp_buf_[0] > 0) {
          response.data.assign(this->resp_buf_ + 1, this->resp_buf_ + 1 + this->resp_buf_[0]);
        }
        this->finish_transaction_(true, response);
      } else {
        // NAK sent: wait for the target to retransmit the response.
        this->state_ = State::RECV_RESPONSE;
        this->resp_len_ = 0;
        this->resp_crc_ = 0;
        this->escape_pending_ = false;
        this->wait_start_ms_ = now;
      }
      break;
    }

    case State::SEND_SYN: {
      // The transaction is already complete; only the echo is outstanding.
      if (byte != EBUS_SYN) {
        ESP_LOGW(TAG, "SYN echo mismatch (received 0x%02X)", byte);
      }
      this->state_ = State::IDLE;
      this->own_bus_free_ = true;
      break;
    }

    case State::IDLE:
      break;
  }
}

void EBUSHub::check_timeouts_(uint32_t now) {
  switch (this->state_) {
    case State::SEND_BYTE:
      if (now - this->sent_ms_ > EBUS_SEND_TIMEOUT_MS) {
        // No echo came back: either another master owns the bus or our TX is
        // not looped back to RX (wrong pins / wiring). No competitor symbol
        // was observed, so no arbitration lockout (ebusd treats the echo
        // timeout like a lost arbitration without lockout).
        this->handle_bus_lost_("echo timeout");
      }
      break;

    case State::SEND_ACK:
      if (now - this->sent_ms_ > EBUS_SEND_TIMEOUT_MS) {
        this->fail_transaction_("ACK echo timeout", false);
      }
      break;

    case State::SEND_SYN:
      if (now - this->sent_ms_ > EBUS_SEND_TIMEOUT_MS) {
        // The transaction is complete; just release the bus.
        this->state_ = State::IDLE;
        this->own_bus_free_ = true;
      }
      break;

    case State::WAIT_ACK:
      if (now - this->wait_start_ms_ > EBUS_SYN_TIMEOUT_MS) {
        // The target may still answer late (see EBUS_DRAIN_MS): drain first.
        this->drain_until_ms_ = now + EBUS_DRAIN_MS;
        this->fail_transaction_("ACK timeout", true);
      }
      break;

    case State::RECV_RESPONSE:
    case State::RECV_RESPONSE_CRC: {
      uint32_t timeout = (this->state_ == State::RECV_RESPONSE && this->resp_len_ == 0)
                             ? EBUS_SYN_TIMEOUT_MS
                             : EBUS_SLAVE_RECV_TIMEOUT_MS;
      if (now - this->wait_start_ms_ > timeout) {
        this->drain_until_ms_ = now + EBUS_DRAIN_MS;
        this->fail_transaction_("response timeout", true);
      }
      break;
    }

    case State::IDLE:
      break;
  }
}

#ifdef USE_LIGHT
void EBUSHub::update_status_led_(uint32_t now) {
  if (this->status_led_ == nullptr || now < this->status_led_next_check_ms_) {
    return;
  }
  this->status_led_next_check_ms_ = now + EBUS_STATUS_LED_CHECK_MS;

  StatusLedState desired;
  if (this->listen_only_) {
    desired = StatusLedState::LISTEN_ONLY;
  } else if (now - this->last_rx_ms_ > EBUS_STATUS_LED_DEAD_MS) {
    desired = StatusLedState::BUS_DEAD;
  } else {
    desired = StatusLedState::BUS_ALIVE;
  }
  if (desired == this->status_led_state_) {
    return;
  }
  this->status_led_state_ = desired;

  float red = 0.0f, green = 0.0f, blue = 0.0f;
  switch (desired) {
    case StatusLedState::BUS_ALIVE:
      green = 1.0f;
      break;
    case StatusLedState::BUS_DEAD:
      red = 1.0f;
      break;
    case StatusLedState::LISTEN_ONLY:
      blue = 1.0f;
      break;
    case StatusLedState::NONE:
      break;
  }
  auto call = this->status_led_->turn_on();
  call.set_color_mode(light::ColorMode::RGB);
  call.set_red(red).set_green(green).set_blue(blue);
  call.set_brightness(EBUS_STATUS_LED_BRIGHTNESS);
  call.set_transition_length(250);
  call.perform();
}
#endif

}  // namespace ebus
}  // namespace esphome

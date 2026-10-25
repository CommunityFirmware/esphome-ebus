#pragma once

// eBUS transport hub.
//
// Owns the UART connection to the eBUS interface, arbitrates bus access and
// runs the direct-mode master transaction state machine (per-byte echo check,
// arbitration lockout after a lost arbitration, ACK/NAK handshake, response
// reception with CRC check, SYN end-of-message). While idle, all received
// bytes are fed to a passive decoder so that traffic of other bus nodes can
// be monitored.
//
// Reference: https://github.com/Project-Helianthus/helianthus-docs-ebus
//            protocols/ebus-services/ebus-overview.md
//            (direct-mode transaction flow, per-byte echo, ACK/NAK handling,
//            initiator-to-initiator transactions, SYN timeout semantics)

#include <cstdint>
#include <functional>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_LIGHT
#include "esphome/components/light/light_state.h"
#endif

#include "ebus_const.h"
#include "ebus_frame.h"

namespace esphome {
namespace ebus {

// A response received from a target, with logical (unescaped) payload.
struct EBUSResponse {
  uint8_t src{0};  // responder address
  uint8_t dst{0};  // our address
  uint8_t pb{0};
  uint8_t sb{0};
  std::vector<uint8_t> data;
};

// Invoked from loop() when a queued request completes. `success` is false on
// NAK, timeout, CRC error or lost arbitration; `response` is only populated
// on success.
using EBUSRequestCallback = std::function<void(bool success, const EBUSResponse &response)>;

class EBUSHub : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  void set_address(uint8_t address) { this->address_ = address; }
  uint8_t get_address() const { return this->address_; }

  // Bring-up/debug mode: never transmit, only passively decode bus traffic.
  void set_listen_only(bool listen_only) { this->listen_only_ = listen_only; }
  bool is_listen_only() const { return this->listen_only_; }

  // Arbitration delay in microseconds (0-620): time to yield after the bus is
  // observed free before starting a telegram (see ebus_const.h).
  void set_arbitration_delay(uint32_t arbitration_delay_us) {
    this->arbitration_delay_us_ = arbitration_delay_us;
  }

#ifdef USE_LIGHT
  // Optional status LED (any RGB light entity). The hub drives it from
  // loop(): green = bus traffic seen recently, red = bus silent (not
  // connected or stuck), blue = listen-only mode.
  void set_status_led(light::LightState *status_led) { this->status_led_ = status_led; }
#endif

  // Queue a request to `dst`. The callback is invoked from loop() once the
  // transaction completes. Returns false if the queue is full.
  bool send_request(uint8_t dst, uint8_t pb, uint8_t sb, const std::vector<uint8_t> &data,
                    EBUSRequestCallback &&callback);

  // Queue a broadcast telegram (no ACK/response is expected).
  bool send_broadcast(uint8_t pb, uint8_t sb, const std::vector<uint8_t> &data);

  // Passive monitoring of traffic initiated by other bus nodes.
  void add_on_frame_callback(std::function<void(const EBUSDecodedFrame &)> &&callback);
  void add_on_response_callback(std::function<void(const EBUSDecodedFrame &)> &&callback);

  // Statistics (also reported by dump_config()).
  uint32_t get_stat_success() const { return this->stat_success_; }
  uint32_t get_stat_failed() const { return this->stat_failed_; }
  uint32_t get_stat_bus_lost() const { return this->stat_bus_lost_; }
  uint32_t get_stat_crc_errors() const { return this->stat_crc_errors_; }
  uint32_t get_stat_frames_seen() const { return this->stat_frames_seen_; }
  uint32_t get_stat_responses_seen() const { return this->stat_responses_seen_; }
  uint32_t get_stat_rx_bytes() const { return this->stat_rx_bytes_; }

 protected:
  enum class State : uint8_t {
    IDLE,
    SEND_BYTE,          // telegram byte written, waiting for its echo
    WAIT_ACK,           // waiting for the target's ACK/NAK
    RECV_RESPONSE,      // receiving the response payload (LEN DATA...)
    RECV_RESPONSE_CRC,  // receiving the response CRC byte
    SEND_ACK,           // ACK/NAK for the response written, waiting for its echo
    SEND_SYN,           // SYN written, waiting for its echo
  };

  struct PendingRequest {
    uint8_t dst{0};
    uint8_t pb{0};
    uint8_t sb{0};
    uint8_t data[EBUS_MAX_DATA_LEN]{};
    size_t data_len{0};
    // Escaped telegram including CRC; worst case every byte is escaped.
    uint8_t wire[2 * (6 + EBUS_MAX_DATA_LEN)]{};
    size_t wire_len{0};
    EBUSRequestCallback callback;
    uint8_t bus_lost_retries{0};
    bool nak_repeat{false};       // a NAK-triggered resend is in progress
    bool resp_nak_repeat{false};  // a response-NAK-triggered resend is in progress
  };

  void start_transaction_();
  void send_current_byte_();
  void send_syn_();
  void finish_transaction_(bool success, const EBUSResponse &response);
  void fail_transaction_(const char *reason, bool release_bus);
  // Called when another initiator wins the bus (echo mismatch) or no echo
  // arrives at all. `competing_byte` is the colliding symbol when observed;
  // it determines the arbitration lockout (see ebus_const.h).
  void handle_bus_lost_(const char *reason, const uint8_t *competing_byte = nullptr);
  void handle_rx_byte_(uint8_t byte, uint32_t now);
  void check_timeouts_(uint32_t now);
  bool bus_idle_(uint32_t now) const;
#ifdef USE_LIGHT
  void update_status_led_(uint32_t now);
#endif

  uint8_t address_{0x31};
  bool listen_only_{false};

  State state_{State::IDLE};
  // Fixed-capacity request queue (no per-request heap allocation). Must hold
  // the setup burst (identification + system info + discovery: 27 requests)
  // plus the polling component's in-flight window (8) with headroom for
  // user-triggered writes. A request that loses arbitration stays in current_
  // and is retried in place, so no push-front is needed.
  static const size_t MAX_QUEUE_LEN = 40;
  StaticRingBuffer<PendingRequest, MAX_QUEUE_LEN> queue_;
  PendingRequest current_{};
  bool current_valid_{false};

  // current transaction progress
  size_t send_pos_{0};
  uint32_t sent_ms_{0};       // when the current outbound byte was written
  uint32_t wait_start_ms_{0}; // start of the current wait (ACK / response)
  uint8_t ack_byte_{EBUS_ACK};

  // response reception (logical bytes: LEN DATA...)
  uint8_t resp_buf_[1 + EBUS_MAX_DATA_LEN];
  size_t resp_len_{0};
  uint8_t resp_crc_{0};
  bool escape_pending_{false};

  // bus activity tracking
  uint32_t last_rx_ms_{0};
  bool own_bus_free_{false};  // set when we just finished our own transaction
  // Remaining bus-free phases to wait out after a lost arbitration before
  // the next transaction may start (arbitration lockout, see ebus_const.h).
  uint8_t syn_lockout_{0};
  // True when the last received byte in IDLE state was a standalone SYN
  // (AUTO-SYN generator or end-of-message): the bus is free on the SYN.
  bool last_rx_was_syn_{false};
  // Do not start a new telegram before this time (late-answer drain, see
  // EBUS_DRAIN_MS): a timed-out target may still put its answer on the bus.
  uint32_t drain_until_ms_{0};
  // Arbitration delay window (see ebus_const.h): stamped on the first loop
  // iteration that observes a free bus with a pending request; the telegram
  // starts once the window has elapsed. Any RX activity during the window
  // makes bus_idle_() false again and restarts it.
  bool bus_free_stamped_{false};
  uint32_t bus_free_at_us_{0};
  uint32_t arbitration_delay_us_{EBUS_ARBITRATION_DELAY_US};

  PassiveFrameDecoder decoder_;
  std::vector<std::function<void(const EBUSDecodedFrame &)>> frame_callbacks_;
  std::vector<std::function<void(const EBUSDecodedFrame &)>> response_callbacks_;

#ifdef USE_LIGHT
  light::LightState *status_led_{nullptr};
  enum class StatusLedState : uint8_t {
    NONE,         // not driven yet
    BUS_ALIVE,    // green: traffic (or AUTO-SYN) seen recently
    BUS_DEAD,     // red: no RX for EBUS_STATUS_LED_DEAD_MS
    LISTEN_ONLY,  // blue: passive monitoring, never transmits
  };
  StatusLedState status_led_state_{StatusLedState::NONE};
  uint32_t status_led_next_check_ms_{0};
#endif

  // statistics
  uint32_t stat_success_{0};
  uint32_t stat_failed_{0};
  uint32_t stat_bus_lost_{0};
  uint32_t stat_crc_errors_{0};
  uint32_t stat_frames_seen_{0};
  uint32_t stat_responses_seen_{0};
  uint32_t stat_rx_bytes_{0};
};

}  // namespace ebus
}  // namespace esphome

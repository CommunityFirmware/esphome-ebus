#pragma once

// Vaillant VRC720 system regulator device (B524 protocol over eBUS).
//
// Polls the registers that user-configured entities subscribe to, decodes the
// B524 replies and dispatches the values to the entities. Writes (number /
// select / button) are queued on the eBUS hub; after a successful write the
// register is re-read so subscribed entities refresh from the device.
//
// Reference: https://github.com/Project-Helianthus/helianthus-docs-ebus
//            protocols/vaillant/ebus-vaillant-B524.md

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/ebus/ebus.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_CLIMATE
#include "esphome/components/climate/climate.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include "vaillant_b524.h"

namespace esphome {
namespace vaillant {

class VaillantVRC720;

// A decoded register value delivered to entity subscriptions.
struct VRC720Value {
  bool valid{false};
  float f32{0};
  int32_t integer{0};
  bool boolean{false};
  std::string text;
};

using VRC720ValueCallback = std::function<void(const VRC720Value &value)>;

// Write payload encodings (must match the write_encoding values in registers.py).
enum class B524WriteEncoding : uint8_t {
  F32,
  U16,
  U8,
};

class VaillantVRC720 final : public PollingComponent {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_ebus_hub(ebus::EBUSHub *hub) { this->hub_ = hub; }
  void set_address(uint8_t address) { this->address_ = address; }
  uint8_t get_address() const { return this->address_; }

  // Subscribe to register updates. One read request is sent per unique
  // (op, gg, rr, instance) key per poll cycle; every matching subscription is
  // invoked with the decoded value.
  void add_subscription(uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance, B524ValueType value_type,
                        VRC720ValueCallback &&callback);

  // Queue a B524 write. After a successful write the register is re-read (when
  // something subscribes to it) so entities refresh from the device.
  void write_register(uint8_t op, uint8_t gg, uint16_t rr, uint8_t write_instance,
                      const std::vector<uint8_t> &value, uint8_t read_instance);

 protected:
  struct Subscription {
    uint8_t op;
    uint8_t gg;
    uint16_t rr;
    uint8_t instance;
    B524ValueType value_type;
    VRC720ValueCallback callback;
  };

  struct PendingPoll {
    uint8_t op;
    uint8_t gg;
    uint16_t rr;
    uint8_t instance;
  };

  void queue_read_(uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance);
  void handle_read_response_(uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance,
                             const std::vector<uint8_t> &data);
  VRC720Value decode_value_(B524ValueType value_type, const std::vector<uint8_t> &value);
  bool has_subscription_(uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance) const;
  void pump_poll_();
  void handle_passive_frame_(const ebus::EBUSDecodedFrame &frame);
  void handle_passive_response_(const ebus::EBUSDecodedFrame &response);

  ebus::EBUSHub *hub_{nullptr};
  uint8_t address_{0x15};

  // A B524 read request directed at our device that was passively observed on
  // the bus; the response that follows is decoded and dispatched to the
  // matching subscriptions. eBUS is half-duplex: a request is immediately
  // followed by its ACK/response, so a single slot is enough for correlation.
  struct PassiveRead {
    bool active{false};
    uint8_t op{0};
    uint8_t gg{0};
    uint16_t rr{0};
    uint8_t instance{0};
  };
  PassiveRead passive_read_;

  std::vector<Subscription> subscriptions_;
  // Fixed-capacity poll queue; sized to comfortably hold every unique
  // subscription key of one poll cycle.
  StaticRingBuffer<PendingPoll, 32> poll_queue_;
  bool poll_active_{false};
  uint8_t poll_in_flight_{0};
  static const uint8_t MAX_POLL_IN_FLIGHT = 8;
};

// Mixin for entities backed by a VRC720 register.
class VRC720EntityBase {
 public:
  virtual ~VRC720EntityBase() = default;

  void set_vrc720_parent(VaillantVRC720 *parent, uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance,
                         B524ValueType value_type, bool subscribe = true) {
    this->parent_ = parent;
    this->op_ = op;
    this->gg_ = gg;
    this->rr_ = rr;
    this->instance_ = instance;
    this->value_type_ = value_type;
    if (subscribe) {
      parent->add_subscription(op, gg, rr, instance, value_type,
                               [this](const VRC720Value &value) { this->on_vrc720_value_(value); });
    }
  }

 protected:
  virtual void on_vrc720_value_(const VRC720Value &value) = 0;

  VaillantVRC720 *parent_{nullptr};
  uint8_t op_{0};
  uint8_t gg_{0};
  uint16_t rr_{0};
  uint8_t instance_{0};
  B524ValueType value_type_{B524ValueType::RAW};
};

#ifdef USE_SENSOR
class VRC720Sensor final : public sensor::Sensor, public VRC720EntityBase {
 protected:
  void on_vrc720_value_(const VRC720Value &value) override;
};
#endif

#ifdef USE_BINARY_SENSOR
class VRC720BinarySensor final : public binary_sensor::BinarySensor, public VRC720EntityBase {
 public:
  // Invert the published state (registers that report "ok" semantics but are
  // exposed with a device class where ON means problem).
  void set_inverted(bool inverted) { this->inverted_ = inverted; }

 protected:
  void on_vrc720_value_(const VRC720Value &value) override;

  bool inverted_{false};
};
#endif

#ifdef USE_NUMBER
class VRC720Number final : public number::Number, public VRC720EntityBase {
 public:
  void set_write_config(uint8_t write_instance, B524WriteEncoding write_encoding) {
    this->write_instance_ = write_instance;
    this->write_encoding_ = write_encoding;
  }
  void control(float value) override;

 protected:
  void on_vrc720_value_(const VRC720Value &value) override;

  uint8_t write_instance_{0};
  B524WriteEncoding write_encoding_{B524WriteEncoding::F32};
};
#endif

#ifdef USE_SELECT
class VRC720Select final : public select::Select, public VRC720EntityBase {
 public:
  void set_write_config(uint8_t write_instance, B524WriteEncoding write_encoding,
                        const std::vector<uint16_t> &option_values) {
    this->write_instance_ = write_instance;
    this->write_encoding_ = write_encoding;
    this->option_values_ = option_values;
  }
  void control(size_t index) override;

 protected:
  void on_vrc720_value_(const VRC720Value &value) override;

  uint8_t write_instance_{0};
  B524WriteEncoding write_encoding_{B524WriteEncoding::U16};
  std::vector<uint16_t> option_values_;
};
#endif

#ifdef USE_TEXT_SENSOR
class VRC720TextSensor final : public text_sensor::TextSensor, public VRC720EntityBase {
 protected:
  void on_vrc720_value_(const VRC720Value &value) override;
};
#endif

#ifdef USE_BUTTON
class VRC720Button final : public button::Button, public VRC720EntityBase {
 public:
  void set_write_config(uint8_t write_instance, uint8_t button_value) {
    this->write_instance_ = write_instance;
    this->button_value_ = button_value;
  }

 protected:
  void press_action() override;
  // Buttons are write-only commands and never subscribe to register reads.
  void on_vrc720_value_(const VRC720Value &value) override {}

  uint8_t write_instance_{0};
  uint8_t button_value_{0};
};
#endif

// A climate entity composed from several VRC720 registers: current/target
// temperature, (zone: humidity,) the operating mode and the special-function
// state. Unlike the single-register entities above it subscribes itself.
//
// Zone variant (zone 0..0x0A):
//   current_temperature  <- zone_room_temperature (RR=0x000F)
//   current_humidity     <- zone_room_humidity (RR=0x0028)
//   target_temperature   <- heating_zone_room_manual_temperature_setpoint (RR=0x0022)
//   mode                 <- heating_zone_operating_mode (RR=0x0006):
//                           0 off -> OFF, 1 time_controlled -> AUTO, 2 manual -> HEAT
//   preset               <- zone_special_function_state (RR=0x001B):
//                           4 QuickVeto -> BOOST, 7 Holiday -> AWAY (reported
//                           only; the away write sequence is not implemented),
//                           otherwise NONE
//   control: mode -> write RR=0x0006, target temperature -> write RR=0x0022,
//   BOOST -> quick veto (write RR=0x0008 then RR=0x0026, protocol doc 4.9),
//   NONE (cancel) -> write RR=0x000E = 0.
//
// DHW variant:
//   current_temperature  <- dhw_temperature (RR=0x0005)
//   target_temperature   <- dhw_temperature_setpoint (RR=0x0004)
//   mode                 <- dhw_operating_mode (RR=0x0003), same mapping
//   preset               <- dhw_special_function_state (RR=0x000F):
//                           9 OneTimeTankloading -> BOOST, otherwise NONE
//   action               <- dhw_pump_active (RR=0x0006): HEATING / IDLE
//   control: mode -> write RR=0x0003, target temperature -> write RR=0x0004,
//   BOOST -> write RR=0x000D = 6 (one-time load), NONE -> write RR=0x000D = 0.
// The entity does not publish until the operating mode has been read once
// (mode_known_): the default Climate mode is OFF, and publishing it before the
// mode register answers would show a spurious "off" in HA.
#ifdef USE_CLIMATE
class VRC720Climate final : public climate::Climate, public Component {
 public:
  void set_vrc720_parent(VaillantVRC720 *parent, uint8_t zone, bool dhw, float quick_veto_duration);
  climate::ClimateTraits traits() override { return this->traits_; }
  void control(const climate::ClimateCall &call) override;

 protected:
  void on_mode_value_(const VRC720Value &value);
  void on_setpoint_value_(const VRC720Value &value);
  void on_current_value_(const VRC720Value &value);
  void on_humidity_value_(const VRC720Value &value);
  void on_special_function_value_(const VRC720Value &value);
  void on_pump_value_(const VRC720Value &value);
  void update_action_();
  void write_(uint16_t rr, B524WriteEncoding encoding, float value);

  VaillantVRC720 *parent_{nullptr};
  uint8_t zone_{0};
  bool dhw_{false};
  float quick_veto_duration_{3.0f};
  bool pump_active_{false};
  bool mode_known_{false};
  climate::ClimateTraits traits_;
};
#endif

}  // namespace vaillant
}  // namespace esphome

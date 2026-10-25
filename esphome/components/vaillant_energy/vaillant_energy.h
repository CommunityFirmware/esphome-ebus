#pragma once

// Vaillant energy statistics component (B516, PB=0xB5 SB=0x16). (PB=0xB5 SB=0x16).
//
// Polls the energy statistics service of a Vaillant regulator (sub-command
// 0x10: cumulative watt-hours per source/usage/period) and, optionally, the
// candidate sub-ID 0x14 "current electrical power" of a heat pump / backup
// heater device. Values are delivered to subscribed sensor entities.
//
// Reference: https://github.com/Project-Helianthus/helianthus-docs-ebus
//            protocols/vaillant/ebus-vaillant-B516-energy.md

#include <cstdint>
#include <functional>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/ebus/ebus.h"
#include "esphome/components/sensor/sensor.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include "vaillant_b516.h"

namespace esphome {
namespace vaillant {

// Outcome of one B516 query, delivered to the entity subscription.
enum class EnergyValueState : uint8_t {
  OK,                  // value follows (statistics: Wh, power: W)
  UNAVAILABLE,         // the device answered "no value" (return code != 0)
  TRANSPORT_FAILED,    // no usable reply (timeout, arbitration, CRC, shape)
};

class VaillantEnergy final : public PollingComponent {
 public:
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_ebus_hub(ebus::EBUSHub *hub) { this->hub_ = hub; }
  void set_address(uint8_t address) { this->address_ = address; }
#ifdef USE_TIME
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
#endif

  // Register a query. `target` 0 means the component's own address. The
  // callback receives the state and the value (Wh for statistics, candidate
  // watts for sub-ID 0x14).
  void add_subscription(uint8_t sub, uint8_t period, uint8_t source, uint8_t usage,
                        uint16_t packed_date, bool date_fixed, uint8_t target,
                        std::function<void(EnergyValueState state, float value)> &&callback);

 protected:
  struct Subscription {
    uint8_t sub;
    uint8_t period;
    uint8_t source;
    uint8_t usage;
    uint16_t packed_date;
    bool date_fixed;
    uint8_t target;
    std::function<void(EnergyValueState state, float value)> callback;
  };

  bool today_packed_(uint16_t *packed);
  void poll_one_(size_t index);

  ebus::EBUSHub *hub_{nullptr};
  uint8_t address_{0x15};
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
  bool time_warned_{false};
  std::vector<Subscription> subscriptions_;
};

// A sensor backed by one B516 query: either energy statistics (value
// published in kWh) or the candidate current-electrical-power sub-ID 0x14
// (value published in W).
class EnergySensor final : public sensor::Sensor, public Component {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_parent(VaillantEnergy *parent) { this->parent_ = parent; }
  void set_target_address(uint8_t address) { this->target_ = address; }
  void set_statistics(uint8_t period, uint8_t source, uint8_t usage, uint16_t year, uint8_t month,
                      uint8_t day, bool date_fixed);
  void set_power();

 protected:
  void on_value_(EnergyValueState state, float value);

  VaillantEnergy *parent_{nullptr};
  uint8_t sub_{B516_SUB_STATISTICS};
  uint8_t period_{B516_PERIOD_TOTAL};
  uint8_t source_{0};
  uint8_t usage_{0};
  uint16_t packed_date_{0};
  bool date_fixed_{false};
  uint8_t target_{0};
};

}  // namespace vaillant
}  // namespace esphome

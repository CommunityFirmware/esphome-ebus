#include "vaillant_energy.h"

#include "esphome/core/log.h"

namespace esphome {
namespace vaillant {

static const char *const TAG = "vaillant_energy";

void VaillantEnergy::add_subscription(uint8_t sub, uint8_t period, uint8_t source, uint8_t usage,
                                    uint16_t packed_date, bool date_fixed, uint8_t target,
                                    std::function<void(EnergyValueState state, float value)> &&callback) {
  this->subscriptions_.push_back({sub, period, source, usage, packed_date, date_fixed, target,
                                  std::move(callback)});
}

bool VaillantEnergy::today_packed_(uint16_t *packed) {
#ifdef USE_TIME
  if (this->time_ != nullptr) {
    const ESPTime now = this->time_->now();
    if (now.is_valid()) {
      *packed = b516_pack_date(now.year, now.month, now.day_of_month);
      return true;
    }
  }
#endif
  return false;
}

void VaillantEnergy::update() {
  if (this->hub_ == nullptr || this->hub_->is_listen_only()) {
    return;
  }
  LOG_UPDATE_INTERVAL(this);
  for (size_t i = 0; i < this->subscriptions_.size(); i++) {
    this->poll_one_(i);
  }
}

void VaillantEnergy::poll_one_(size_t index) {
  const Subscription &sub = this->subscriptions_[index];

  std::vector<uint8_t> payload;
  if (sub.sub == B516_SUB_STATISTICS) {
    uint16_t date = sub.packed_date;
    if (!sub.date_fixed) {
      if (sub.period == B516_PERIOD_TOTAL) {
        date = 0;  // ignored by the regulator for totals
      } else if (!this->today_packed_(&date)) {
        // Daily windows need today's date; without a valid time source the
        // request would query an arbitrary day. Warn once and skip.
        if (!this->time_warned_) {
          this->time_warned_ = true;
          ESP_LOGW(TAG, "Skipping daily energy statistics: no valid time source configured");
        }
        return;
      }
    }
    payload = b516_build_statistics_request(sub.period, sub.source, sub.usage, date);
  } else {
    payload = {sub.sub};
  }

  const uint8_t target = sub.target != 0 ? sub.target : this->address_;
  bool queued = this->hub_->send_request(
      target, B516_PB, B516_SB, payload,
      [this, index](bool success, const ebus::EBUSResponse &response) {
        const Subscription &s = this->subscriptions_[index];
        if (!success) {
          s.callback(EnergyValueState::TRANSPORT_FAILED, 0.0f);
          return;
        }
        if (s.sub == B516_SUB_STATISTICS) {
          auto reply = b516_parse_statistics_reply(response.data);
          if (!reply.valid) {
            ESP_LOGW(TAG, "Statistics reply has unexpected length %u",
                     static_cast<unsigned>(response.data.size()));
            s.callback(EnergyValueState::TRANSPORT_FAILED, 0.0f);
          } else if (!reply.ok) {
            s.callback(EnergyValueState::UNAVAILABLE, 0.0f);
          } else {
            s.callback(EnergyValueState::OK, reply.wh);
          }
        } else {
          float watts;
          if (b516_parse_power_reply(response.data, &watts)) {
            s.callback(EnergyValueState::OK, watts);
          } else {
            ESP_LOGW(TAG, "Power reply has unexpected length %u",
                     static_cast<unsigned>(response.data.size()));
            s.callback(EnergyValueState::TRANSPORT_FAILED, 0.0f);
          }
        }
      });
  if (!queued) {
    ESP_LOGW(TAG, "eBUS hub queue full, dropping B516 query (sub=0x%02X)", sub.sub);
  }
}

void VaillantEnergy::dump_config() {
  ESP_LOGCONFIG(TAG, "Vaillant Energy:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
#ifdef USE_TIME
  ESP_LOGCONFIG(TAG, "  Time source: %s", this->time_ == nullptr ? "none" : "configured");
#else
  ESP_LOGCONFIG(TAG, "  Time source: none (component built without time support)");
#endif
  ESP_LOGCONFIG(TAG, "  Subscriptions: %u", static_cast<unsigned>(this->subscriptions_.size()));
}

// ---- sensor entity -----------------------------------------------------------

void EnergySensor::set_statistics(uint8_t period, uint8_t source, uint8_t usage, uint16_t year,
                                uint8_t month, uint8_t day, bool date_fixed) {
  this->sub_ = B516_SUB_STATISTICS;
  this->period_ = period;
  this->source_ = source;
  this->usage_ = usage;
  this->packed_date_ = b516_pack_date(year, month, day);
  this->date_fixed_ = date_fixed;
}

void EnergySensor::set_power() { this->sub_ = B516_SUB_POWER; }

void EnergySensor::setup() {
  this->parent_->add_subscription(
      this->sub_, this->period_, this->source_, this->usage_, this->packed_date_, this->date_fixed_,
      this->target_, [this](EnergyValueState state, float value) { this->on_value_(state, value); });
}

void EnergySensor::on_value_(EnergyValueState state, float value) {
  switch (state) {
    case EnergyValueState::OK:
      // Statistics values are watt-hours on the wire; publish kWh.
      this->publish_state(this->sub_ == B516_SUB_STATISTICS ? value / 1000.0f : value);
      break;
    case EnergyValueState::UNAVAILABLE:
      // The device has no value for this selector: unknown, not zero.
      this->publish_state(NAN);
      break;
    case EnergyValueState::TRANSPORT_FAILED:
      // Keep the last state; the next poll cycle retries.
      break;
  }
}

}  // namespace vaillant
}  // namespace esphome

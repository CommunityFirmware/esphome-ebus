#include "vaillant_vrc720.h"

#include <cmath>

#include "esphome/core/log.h"

namespace esphome {
namespace vaillant {

static const char *const TAG = "vaillant_vrc720";

// Standard eBUS identification service (empty payload, 10-byte reply).
static const uint8_t IDENTIFY_PB = 0x07;
static const uint8_t IDENTIFY_SB = 0x04;

// Topology discovery selectors (register map, VRC720 profile intervals).
static const uint8_t DISCOVER_GG_DHW = 0x01;
static const uint16_t DISCOVER_RR_DHW_CIRCUIT_TYPE = 0x0001;
static const uint8_t DISCOVER_GG_CIRCUITS = 0x02;
static const uint16_t DISCOVER_RR_CIRCUIT_TYPE = 0x0002;
static const uint8_t DISCOVER_GG_ZONES = 0x03;
static const uint16_t DISCOVER_RR_ZONE_CIRCUIT_ASSIGNMENT = 0x001C;
static const uint8_t DISCOVER_CIRCUIT_MAX = 9;   // II 0x00..0x09 (0x09 = virtual native water)
static const uint8_t DISCOVER_ZONE_MAX = 0x0A;   // II 0x00..0x0A

// OP=0x00 ReadSystemInformation identifiers (register map).
static const uint16_t SYSINFO_CIRCUIT_COUNT_MAXIMUM = 0x0000;
static const uint16_t SYSINFO_ZONE_COUNT_MAXIMUM = 0x0001;
static const uint16_t SYSINFO_INTERFACE_VERSION = 0x0006;
static const uint16_t SYSINFO_INTERFACE_REVISION = 0x0007;

void VaillantVRC720::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Vaillant VRC720 at address 0x%02X...", this->address_);
  if (this->hub_ == nullptr) {
    ESP_LOGE(TAG, "No eBUS hub configured");
    this->mark_failed();
    return;
  }

  // Passive monitoring: when other bus nodes read our device's registers, the
  // request/response pair passes through the hub's passive decoder. Track the
  // request here and dispatch the response to the matching subscriptions, so
  // entities update even while we never transmit (listen-only bring-up).
  this->hub_->add_on_frame_callback(
      [this](const ebus::EBUSDecodedFrame &frame) { this->handle_passive_frame_(frame); });
  this->hub_->add_on_response_callback(
      [this](const ebus::EBUSDecodedFrame &response) { this->handle_passive_response_(response); });

  if (this->hub_->is_listen_only()) {
    ESP_LOGW(TAG,
             "eBUS hub is in listen-only mode: no identification, discovery or polling requests "
             "are sent; entities update from passively observed traffic only");
    return;
  }

  // Identification (standard eBUS identify).
  this->hub_->send_request(this->address_, IDENTIFY_PB, IDENTIFY_SB, {},
                           [this](bool success, const ebus::EBUSResponse &response) {
                             if (!success) {
                               ESP_LOGW(TAG, "Identification request failed");
                               return;
                             }
                             auto id = b524_parse_identification(response.data);
                             if (!id.valid) {
                               ESP_LOGW(TAG, "Identification reply has unexpected length %u",
                                        static_cast<unsigned>(response.data.size()));
                               return;
                             }
                             std::string model = b524_identification_model(id.device_id, id.spn);
                             ESP_LOGI(TAG, "Identified: manufacturer=0x%02X device_id=%s sw=%02X.%02X SPN=%u model=%s",
                                      id.manufacturer, id.device_id.c_str(), id.sw_version, id.sw_revision,
                                      id.spn, model.empty() ? "unknown" : model.c_str());
                           });

  // System information (OP=0x00).
  for (uint16_t info_id : {SYSINFO_CIRCUIT_COUNT_MAXIMUM, SYSINFO_ZONE_COUNT_MAXIMUM,
                           SYSINFO_INTERFACE_VERSION, SYSINFO_INTERFACE_REVISION}) {
    auto payload = b524_build_system_info_request(info_id);
    this->hub_->send_request(this->address_, B524_PB, B524_SB, payload,
                             [this, info_id](bool success, const ebus::EBUSResponse &response) {
                               if (!success) {
                                 return;
                               }
                               float value;
                               if (b524_parse_system_info_reply(response.data, &value)) {
                                 ESP_LOGI(TAG, "System info 0x%04X = %.2f", info_id, value);
                               }
                             });
  }

  // Topology discovery: DHW circuit, heating circuits, zones.
  auto dhw_payload = b524_build_read_request(B524_OP_GET_PARAMETER, DISCOVER_GG_DHW, 0x00,
                                             DISCOVER_RR_DHW_CIRCUIT_TYPE);
  this->hub_->send_request(this->address_, B524_PB, B524_SB, dhw_payload,
                           [this](bool success, const ebus::EBUSResponse &response) {
                             if (!success) {
                               return;
                             }
                             auto reply = b524_parse_register_reply(response.data, DISCOVER_GG_DHW,
                                                                    DISCOVER_RR_DHW_CIRCUIT_TYPE);
                             if (reply.valid) {
                               uint16_t type;
                               if (b524_decode_u16(reply.value, &type)) {
                                 ESP_LOGI(TAG, "DHW circuit present (type=%u)", type);
                               }
                             }
                           });

  for (uint8_t ii = 0; ii <= DISCOVER_CIRCUIT_MAX; ii++) {
    auto payload = b524_build_read_request(B524_OP_GET_PARAMETER, DISCOVER_GG_CIRCUITS, ii,
                                           DISCOVER_RR_CIRCUIT_TYPE);
    this->hub_->send_request(this->address_, B524_PB, B524_SB, payload,
                             [this, ii](bool success, const ebus::EBUSResponse &response) {
                               if (!success) {
                                 return;
                               }
                               auto reply = b524_parse_register_reply(response.data, DISCOVER_GG_CIRCUITS,
                                                                      DISCOVER_RR_CIRCUIT_TYPE);
                               if (reply.valid) {
                                 uint16_t type;
                                 if (b524_decode_u16(reply.value, &type)) {
                                   ESP_LOGI(TAG, "Circuit %u present (type=%u)", ii + 1, type);
                                 }
                               }
                             });
  }

  for (uint8_t ii = 0; ii <= DISCOVER_ZONE_MAX; ii++) {
    auto payload = b524_build_read_request(B524_OP_GET_PARAMETER, DISCOVER_GG_ZONES, ii,
                                           DISCOVER_RR_ZONE_CIRCUIT_ASSIGNMENT);
    this->hub_->send_request(this->address_, B524_PB, B524_SB, payload,
                             [this, ii](bool success, const ebus::EBUSResponse &response) {
                               if (!success) {
                                 return;
                               }
                               auto reply = b524_parse_register_reply(response.data, DISCOVER_GG_ZONES,
                                                                      DISCOVER_RR_ZONE_CIRCUIT_ASSIGNMENT);
                               if (reply.valid) {
                                 int8_t assignment;
                                 if (b524_decode_i8(reply.value, &assignment)) {
                                   ESP_LOGI(TAG, "Zone %u present (circuit assignment=%d)", ii + 1,
                                            assignment);
                                 }
                               }
                             });
  }

  // Start the first poll cycle immediately instead of waiting for the first
  // update interval.
  this->update();
}

void VaillantVRC720::update() {
  if (this->hub_ == nullptr) {
    return;
  }
  if (this->hub_->is_listen_only()) {
    // Never queue polls while passive; the passive frame/response callbacks
    // provide values. Queued requests would never transmit and would fill the
    // hub's queue.
    return;
  }
  if (this->poll_active_) {
    ESP_LOGD(TAG, "Previous poll cycle still in flight, skipping");
    return;
  }
  LOG_UPDATE_INTERVAL(this);

  // One read per unique (op, gg, rr, instance) key.
  this->poll_queue_.clear();
  for (const auto &sub : this->subscriptions_) {
    bool found = false;
    for (const auto &pending : this->poll_queue_) {
      if (pending.op == sub.op && pending.gg == sub.gg && pending.rr == sub.rr &&
          pending.instance == sub.instance) {
        found = true;
        break;
      }
    }
    if (!found) {
      this->poll_queue_.push({sub.op, sub.gg, sub.rr, sub.instance});
    }
  }
  this->poll_active_ = true;
  this->poll_in_flight_ = 0;
  this->pump_poll_();
}

void VaillantVRC720::dump_config() {
  ESP_LOGCONFIG(TAG, "Vaillant VRC720:");
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X", this->address_);
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Subscriptions: %u", static_cast<unsigned>(this->subscriptions_.size()));
}

void VaillantVRC720::add_subscription(uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance,
                                      B524ValueType value_type, VRC720ValueCallback &&callback) {
  this->subscriptions_.push_back({op, gg, rr, instance, value_type, std::move(callback)});
}

void VaillantVRC720::write_register(uint8_t op, uint8_t gg, uint16_t rr, uint8_t write_instance,
                                    const std::vector<uint8_t> &value, uint8_t read_instance) {
  if (this->hub_ == nullptr) {
    return;
  }
  if (this->hub_->is_listen_only()) {
    ESP_LOGW(TAG, "Write GG=0x%02X RR=0x%04X ignored: eBUS hub is in listen-only mode", gg, rr);
    return;
  }
  auto payload = b524_build_write_request(op, gg, write_instance, rr, value);
  this->hub_->send_request(this->address_, B524_PB, B524_SB, payload,
                           [this, op, gg, rr, read_instance](bool success, const ebus::EBUSResponse &) {
                             if (!success) {
                               ESP_LOGW(TAG, "Write GG=0x%02X RR=0x%04X failed", gg, rr);
                               return;
                             }
                             ESP_LOGD(TAG, "Write GG=0x%02X RR=0x%04X ok", gg, rr);
                             if (this->has_subscription_(op, gg, rr, read_instance)) {
                               this->queue_read_(op, gg, rr, read_instance);
                             }
                           });
}

void VaillantVRC720::queue_read_(uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance) {
  if (this->hub_ == nullptr) {
    return;
  }
  std::vector<uint8_t> payload;
  if (op == B524_OP_READ_SYSTEM_INFORMATION) {
    payload = b524_build_system_info_request(rr);
  } else {
    payload = b524_build_read_request(op, gg, instance, rr);
  }
  this->hub_->send_request(this->address_, B524_PB, B524_SB, payload,
                           [this, op, gg, rr, instance](bool success, const ebus::EBUSResponse &response) {
                             if (!success) {
                               ESP_LOGD(TAG, "Read GG=0x%02X RR=0x%04X II=0x%02X failed", gg, rr, instance);
                               return;
                             }
                             this->handle_read_response_(op, gg, rr, instance, response.data);
                           });
}

void VaillantVRC720::handle_read_response_(uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance,
                                           const std::vector<uint8_t> &data) {
  for (const auto &sub : this->subscriptions_) {
    if (sub.op != op || sub.gg != gg || sub.rr != rr || sub.instance != instance) {
      continue;
    }
    VRC720Value value;
    if (op == B524_OP_READ_SYSTEM_INFORMATION) {
      // OP=0x00 replies carry the raw float32, no FLAGS header.
      value = this->decode_value_(sub.value_type, data);
    } else {
      auto reply = b524_parse_register_reply(data, gg, rr);
      if (reply.valid) {
        value = this->decode_value_(sub.value_type, reply.value);
      }
    }
    sub.callback(value);
  }
}

VRC720Value VaillantVRC720::decode_value_(B524ValueType value_type, const std::vector<uint8_t> &value) {
  VRC720Value result;
  switch (value_type) {
    case B524ValueType::F32: {
      float f;
      if (b524_decode_f32(value, &f)) {
        result.valid = true;
        result.f32 = f;
      }
      break;
    }
    case B524ValueType::U16: {
      uint16_t v;
      if (b524_decode_u16(value, &v)) {
        result.valid = true;
        result.integer = v;
      }
      break;
    }
    case B524ValueType::U8: {
      uint8_t v;
      if (b524_decode_u8(value, &v)) {
        result.valid = true;
        result.integer = v;
      }
      break;
    }
    case B524ValueType::I8: {
      int8_t v;
      if (b524_decode_i8(value, &v)) {
        result.valid = true;
        result.integer = v;
      }
      break;
    }
    case B524ValueType::U32: {
      uint32_t v;
      if (b524_decode_u32(value, &v)) {
        result.valid = true;
        result.integer = static_cast<int32_t>(v);
      }
      break;
    }
    case B524ValueType::BOOL: {
      bool b;
      if (b524_decode_bool(value, &b)) {
        result.valid = true;
        result.boolean = b;
        result.integer = b ? 1 : 0;
      }
      break;
    }
    case B524ValueType::ENUM16: {
      uint16_t v;
      if (b524_decode_u16(value, &v)) {
        result.valid = true;
        result.integer = v;
      }
      break;
    }
    case B524ValueType::STRING:
      result.valid = true;
      result.text = b524_decode_string(value);
      break;
    case B524ValueType::DATE: {
      std::string s;
      if (b524_decode_date(value, &s)) {
        result.valid = true;
        result.text = s;
      }
      break;
    }
    case B524ValueType::TIME: {
      std::string s;
      if (b524_decode_time(value, &s)) {
        result.valid = true;
        result.text = s;
      }
      break;
    }
    case B524ValueType::VERSION: {
      std::string s;
      if (b524_decode_version(value, &s)) {
        result.valid = true;
        result.text = s;
      }
      break;
    }
    case B524ValueType::RAW:
      result.valid = true;
      break;
  }
  return result;
}

bool VaillantVRC720::has_subscription_(uint8_t op, uint8_t gg, uint16_t rr, uint8_t instance) const {
  for (const auto &sub : this->subscriptions_) {
    if (sub.op == op && sub.gg == gg && sub.rr == rr && sub.instance == instance) {
      return true;
    }
  }
  return false;
}

void VaillantVRC720::pump_poll_() {
  while (this->poll_in_flight_ < MAX_POLL_IN_FLIGHT && !this->poll_queue_.empty()) {
    PendingPoll pending = this->poll_queue_.front();
    this->poll_queue_.pop();
    this->poll_in_flight_++;
    std::vector<uint8_t> payload;
    if (pending.op == B524_OP_READ_SYSTEM_INFORMATION) {
      payload = b524_build_system_info_request(pending.rr);
    } else {
      payload = b524_build_read_request(pending.op, pending.gg, pending.instance, pending.rr);
    }
    bool queued = this->hub_->send_request(
        this->address_, B524_PB, B524_SB, payload,
        [this, pending](bool success, const ebus::EBUSResponse &response) {
          this->poll_in_flight_--;
          if (success) {
            this->handle_read_response_(pending.op, pending.gg, pending.rr, pending.instance,
                                        response.data);
          }
          this->pump_poll_();
        });
    if (!queued) {
      this->poll_in_flight_--;
      ESP_LOGW(TAG, "eBUS hub queue full, dropping poll for GG=0x%02X RR=0x%04X II=0x%02X", pending.gg,
               pending.rr, pending.instance);
    }
  }
  if (this->poll_queue_.empty() && this->poll_in_flight_ == 0) {
    this->poll_active_ = false;
  }
}

// ---- passive monitoring -----------------------------------------------------

void VaillantVRC720::handle_passive_frame_(const ebus::EBUSDecodedFrame &frame) {
  this->passive_read_.active = false;
  if (frame.pb != B524_PB || frame.sb != B524_SB || frame.dst != this->address_) {
    return;
  }
  const uint8_t *d = frame.data;
  size_t n = frame.data_len;
  if (n >= 6 && (d[0] == B524_OP_GET_PARAMETER || d[0] == B524_OP_GET_DEVICE_PARAMETER) &&
      d[1] == B524_OT_READ) {
    this->passive_read_ = {true, d[0], d[2], static_cast<uint16_t>(d[4] | (d[5] << 8)), d[3]};
  } else if (n >= 3 && d[0] == B524_OP_READ_SYSTEM_INFORMATION) {
    this->passive_read_ = {true, B524_OP_READ_SYSTEM_INFORMATION, 0,
                           static_cast<uint16_t>(d[1] | (d[2] << 8)), 0};
  }
}

void VaillantVRC720::handle_passive_response_(const ebus::EBUSDecodedFrame &response) {
  if (!this->passive_read_.active || response.src != this->address_ || response.pb != B524_PB ||
      response.sb != B524_SB) {
    return;
  }
  PassiveRead read = this->passive_read_;
  this->passive_read_.active = false;
  if (!this->has_subscription_(read.op, read.gg, read.rr, read.instance)) {
    return;
  }
  std::vector<uint8_t> data(response.data, response.data + response.data_len);
  ESP_LOGI(TAG, "passive update: OP=0x%02X GG=0x%02X RR=0x%04X II=0x%02X (%u bytes)", read.op,
           read.gg, read.rr, read.instance, static_cast<unsigned>(data.size()));
  this->handle_read_response_(read.op, read.gg, read.rr, read.instance, data);
}

// ---- entities ---------------------------------------------------------------

#ifdef USE_SENSOR
void VRC720Sensor::on_vrc720_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  if (this->value_type_ == B524ValueType::F32) {
    this->publish_state(value.f32);
  } else {
    this->publish_state(static_cast<float>(value.integer));
  }
}
#endif

#ifdef USE_BINARY_SENSOR
void VRC720BinarySensor::on_vrc720_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  bool state;
  if (this->value_type_ == B524ValueType::BOOL) {
    state = value.boolean;
  } else {
    state = value.integer != 0;
  }
  this->publish_state(this->inverted_ ? !state : state);
}
#endif

#ifdef USE_NUMBER
void VRC720Number::on_vrc720_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  if (this->value_type_ == B524ValueType::F32) {
    this->publish_state(value.f32);
  } else {
    this->publish_state(static_cast<float>(value.integer));
  }
}

void VRC720Number::control(float value) {
  std::vector<uint8_t> encoded;
  switch (this->write_encoding_) {
    case B524WriteEncoding::F32:
      b524_encode_f32(value, &encoded);
      break;
    case B524WriteEncoding::U16:
      b524_encode_u16(static_cast<uint16_t>(std::lround(value)), &encoded);
      break;
    case B524WriteEncoding::U8:
      b524_encode_u8(static_cast<uint8_t>(std::lround(value)), &encoded);
      break;
  }
  this->parent_->write_register(this->op_, this->gg_, this->rr_, this->write_instance_, encoded,
                                this->instance_);
}
#endif

#ifdef USE_SELECT
void VRC720Select::on_vrc720_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  for (size_t i = 0; i < this->option_values_.size(); i++) {
    if (this->option_values_[i] == static_cast<uint16_t>(value.integer)) {
      this->publish_state(i);
      return;
    }
  }
  ESP_LOGW(TAG, "Value %d not in option list", value.integer);
}

void VRC720Select::control(size_t index) {
  if (index >= this->option_values_.size()) {
    return;
  }
  uint16_t raw = this->option_values_[index];
  std::vector<uint8_t> encoded;
  if (this->write_encoding_ == B524WriteEncoding::U16) {
    b524_encode_u16(raw, &encoded);
  } else {
    b524_encode_u8(static_cast<uint8_t>(raw), &encoded);
  }
  this->parent_->write_register(this->op_, this->gg_, this->rr_, this->write_instance_, encoded,
                                this->instance_);
}
#endif

#ifdef USE_TEXT_SENSOR
void VRC720TextSensor::on_vrc720_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  this->publish_state(value.text);
}
#endif

#ifdef USE_BUTTON
void VRC720Button::press_action() {
  std::vector<uint8_t> encoded;
  b524_encode_u8(this->button_value_, &encoded);
  this->parent_->write_register(this->op_, this->gg_, this->rr_, this->write_instance_, encoded,
                                this->instance_);
}
#endif

// ---- climate -----------------------------------------------------------------
//
// Register coordinates (register map / protocol doc 4.9). All are OP=0x02.
#ifdef USE_CLIMATE
static const uint8_t CLIMATE_GG_DHW = 0x01;
static const uint8_t CLIMATE_GG_ZONES = 0x03;
static const uint16_t CLIMATE_RR_DHW_MODE = 0x0003;
static const uint16_t CLIMATE_RR_DHW_SETPOINT = 0x0004;
static const uint16_t CLIMATE_RR_DHW_TEMPERATURE = 0x0005;
static const uint16_t CLIMATE_RR_DHW_PUMP_ACTIVE = 0x0006;
static const uint16_t CLIMATE_RR_DHW_SPECIAL_FUNCTION = 0x000D;
static const uint16_t CLIMATE_RR_DHW_SPECIAL_FUNCTION_STATE = 0x000F;
static const uint16_t CLIMATE_RR_ZONE_MODE = 0x0006;
static const uint16_t CLIMATE_RR_ZONE_QUICK_VETO_SETPOINT = 0x0008;
static const uint16_t CLIMATE_RR_ZONE_SPECIAL_FUNCTION = 0x000E;
static const uint16_t CLIMATE_RR_ZONE_TEMPERATURE = 0x000F;
static const uint16_t CLIMATE_RR_ZONE_SPECIAL_FUNCTION_STATE = 0x001B;
static const uint16_t CLIMATE_RR_ZONE_MANUAL_SETPOINT = 0x0022;
static const uint16_t CLIMATE_RR_ZONE_QUICK_VETO_TIME = 0x0026;
static const uint16_t CLIMATE_RR_ZONE_HUMIDITY = 0x0028;

// zone_special_function_state (RR=0x001B), VRC720 family.
static const int32_t ZONE_SF_NONE = 0;
static const int32_t ZONE_SF_QUICK_VETO = 4;
static const int32_t ZONE_SF_HOLIDAY = 7;
// dhw_special_function_state (RR=0x000F), VRC720 family.
static const int32_t DHW_SF_ONE_TIME_TANK_LOADING = 9;

void VRC720Climate::set_vrc720_parent(VaillantVRC720 *parent, uint8_t zone, bool dhw,
                                      float quick_veto_duration) {
  this->parent_ = parent;
  this->zone_ = zone;
  this->dhw_ = dhw;
  this->quick_veto_duration_ = quick_veto_duration;

  const uint8_t gg = dhw ? CLIMATE_GG_DHW : CLIMATE_GG_ZONES;
  const uint8_t instance = dhw ? 0 : zone;

  this->traits_.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO,
                                     climate::CLIMATE_MODE_HEAT});
  this->traits_.set_supported_presets({climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_BOOST});
  this->traits_.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE |
                                  climate::CLIMATE_SUPPORTS_ACTION);
  if (dhw) {
    this->traits_.set_visual_min_temperature(35);
    this->traits_.set_visual_max_temperature(70);
    this->traits_.set_visual_target_temperature_step(1);
  } else {
    this->traits_.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY);
    this->traits_.set_visual_min_temperature(15);
    this->traits_.set_visual_max_temperature(30);
    this->traits_.set_visual_target_temperature_step(0.5f);
  }

  parent->add_subscription(B524_OP_GET_PARAMETER, gg, dhw ? CLIMATE_RR_DHW_MODE : CLIMATE_RR_ZONE_MODE, instance,
                           B524ValueType::ENUM16,
                           [this](const VRC720Value &value) { this->on_mode_value_(value); });
  parent->add_subscription(B524_OP_GET_PARAMETER, gg,
                           dhw ? CLIMATE_RR_DHW_SETPOINT : CLIMATE_RR_ZONE_MANUAL_SETPOINT, instance,
                           B524ValueType::F32,
                           [this](const VRC720Value &value) { this->on_setpoint_value_(value); });
  parent->add_subscription(B524_OP_GET_PARAMETER, gg,
                           dhw ? CLIMATE_RR_DHW_TEMPERATURE : CLIMATE_RR_ZONE_TEMPERATURE, instance,
                           B524ValueType::F32,
                           [this](const VRC720Value &value) { this->on_current_value_(value); });
  parent->add_subscription(
      B524_OP_GET_PARAMETER, gg,
      dhw ? CLIMATE_RR_DHW_SPECIAL_FUNCTION_STATE : CLIMATE_RR_ZONE_SPECIAL_FUNCTION_STATE,
      instance, B524ValueType::U16,
      [this](const VRC720Value &value) { this->on_special_function_value_(value); });
  if (dhw) {
    parent->add_subscription(B524_OP_GET_PARAMETER, gg, CLIMATE_RR_DHW_PUMP_ACTIVE, instance,
                             B524ValueType::BOOL,
                             [this](const VRC720Value &value) { this->on_pump_value_(value); });
  } else {
    parent->add_subscription(B524_OP_GET_PARAMETER, gg, CLIMATE_RR_ZONE_HUMIDITY, instance, B524ValueType::F32,
                             [this](const VRC720Value &value) { this->on_humidity_value_(value); });
  }
}

void VRC720Climate::on_mode_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  switch (value.integer) {
    case 0:
      this->mode = climate::CLIMATE_MODE_OFF;
      break;
    case 1:
      this->mode = climate::CLIMATE_MODE_AUTO;
      break;
    case 2:
      this->mode = climate::CLIMATE_MODE_HEAT;
      break;
    default:
      ESP_LOGW(TAG, "Unknown operating mode %d", value.integer);
      return;
  }
  this->mode_known_ = true;
  this->update_action_();
  this->publish_state();
}

void VRC720Climate::on_setpoint_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  this->target_temperature = value.f32;
  if (this->mode_known_) {
    this->publish_state();
  }
}

void VRC720Climate::on_current_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  this->current_temperature = value.f32;
  if (this->mode_known_) {
    this->publish_state();
  }
}

void VRC720Climate::on_humidity_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  this->current_humidity = value.f32;
  if (this->mode_known_) {
    this->publish_state();
  }
}

void VRC720Climate::on_special_function_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  if (this->dhw_) {
    this->preset = (value.integer == DHW_SF_ONE_TIME_TANK_LOADING) ? climate::CLIMATE_PRESET_BOOST
                                                                   : climate::CLIMATE_PRESET_NONE;
  } else {
    switch (value.integer) {
      case ZONE_SF_QUICK_VETO:
        this->preset = climate::CLIMATE_PRESET_BOOST;
        break;
      case ZONE_SF_HOLIDAY:
        // Reported so HA shows the active holiday; not settable from HA (the
        // away write sequence is a multi-write candidate, not implemented).
        this->preset = climate::CLIMATE_PRESET_AWAY;
        break;
      default:
        this->preset = climate::CLIMATE_PRESET_NONE;
        break;
    }
  }
  if (this->mode_known_) {
    this->publish_state();
  }
}

void VRC720Climate::on_pump_value_(const VRC720Value &value) {
  if (!value.valid) {
    return;
  }
  this->pump_active_ = value.boolean;
  this->update_action_();
  if (this->mode_known_) {
    this->publish_state();
  }
}

void VRC720Climate::update_action_() {
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    this->action = climate::CLIMATE_ACTION_OFF;
  } else if (this->dhw_ && this->pump_active_) {
    this->action = climate::CLIMATE_ACTION_HEATING;
  } else {
    this->action = climate::CLIMATE_ACTION_IDLE;
  }
}

void VRC720Climate::write_(uint16_t rr, B524WriteEncoding encoding, float value) {
  std::vector<uint8_t> encoded;
  switch (encoding) {
    case B524WriteEncoding::F32:
      b524_encode_f32(value, &encoded);
      break;
    case B524WriteEncoding::U16:
      b524_encode_u16(static_cast<uint16_t>(std::lround(value)), &encoded);
      break;
    case B524WriteEncoding::U8:
      b524_encode_u8(static_cast<uint8_t>(std::lround(value)), &encoded);
      break;
  }
  // Zone writes address the zone instance; DHW writes use the broadcast
  // instance 0xFF (register map write templates).
  const uint8_t write_instance = this->dhw_ ? 0xFF : this->zone_;
  this->parent_->write_register(B524_OP_GET_PARAMETER, this->dhw_ ? CLIMATE_GG_DHW : CLIMATE_GG_ZONES, rr,
                                write_instance, encoded, this->dhw_ ? 0 : this->zone_);
}

void VRC720Climate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    const auto mode = *call.get_mode();
    uint16_t raw;
    switch (mode) {
      case climate::CLIMATE_MODE_OFF:
        raw = 0;
        break;
      case climate::CLIMATE_MODE_AUTO:
        raw = 1;
        break;
      case climate::CLIMATE_MODE_HEAT:
        raw = 2;
        break;
      default:
        ESP_LOGW(TAG, "Unsupported climate mode");
        return;
    }
    this->write_(this->dhw_ ? CLIMATE_RR_DHW_MODE : CLIMATE_RR_ZONE_MODE, B524WriteEncoding::U16,
                 raw);
    this->mode = mode;
    this->mode_known_ = true;
    this->update_action_();
  }
  if (call.get_target_temperature().has_value()) {
    const float target = *call.get_target_temperature();
    this->write_(this->dhw_ ? CLIMATE_RR_DHW_SETPOINT : CLIMATE_RR_ZONE_MANUAL_SETPOINT,
                 B524WriteEncoding::F32, target);
    this->target_temperature = target;
  }
  if (call.get_preset().has_value()) {
    const auto preset = *call.get_preset();
    if (preset == climate::CLIMATE_PRESET_BOOST) {
      if (this->dhw_) {
        // Start a one-time DHW load (same write as the one_time_dhw_load button).
        this->write_(CLIMATE_RR_DHW_SPECIAL_FUNCTION, B524WriteEncoding::U8, 6);
      } else {
        // Quick veto: set point first, then duration (protocol doc 4.9,
        // VRC720 family write order).
        if (std::isnan(this->target_temperature)) {
          ESP_LOGW(TAG, "Quick veto requested before the setpoint was read; ignored");
          if (this->mode_known_) {
            this->publish_state();
          }
          return;
        }
        this->write_(CLIMATE_RR_ZONE_QUICK_VETO_SETPOINT, B524WriteEncoding::F32,
                     this->target_temperature);
        this->write_(CLIMATE_RR_ZONE_QUICK_VETO_TIME, B524WriteEncoding::F32,
                     this->quick_veto_duration_);
      }
      this->preset = preset;
    } else {
      // NONE cancels the special function: quick veto / ventilation boost for
      // zones (RR=0x000E = 0), one-time load for DHW (RR=0x000D = 0).
      this->write_(this->dhw_ ? CLIMATE_RR_DHW_SPECIAL_FUNCTION : CLIMATE_RR_ZONE_SPECIAL_FUNCTION,
                   B524WriteEncoding::U8, 0);
      this->preset = climate::CLIMATE_PRESET_NONE;
    }
  }
  if (this->mode_known_) {
    this->publish_state();
  }
}
#endif

}  // namespace vaillant
}  // namespace esphome

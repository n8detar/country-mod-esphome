#include "countrymod.h"

#include "esphome/components/remote_base/lg_protocol.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

#include <algorithm>
#include <cmath>

namespace esphome::countrymod {

static const char *const TAG = "countrymod.climate";

static constexpr uint8_t MODE_BASE_ECO = 0x00;
static constexpr uint8_t MODE_BASE_COOL = 0x01;
static constexpr uint8_t MODE_BASE_FAN = 0x03;
static constexpr uint8_t MODE_BASE_HEAT = 0x04;

static constexpr uint8_t POWER_BIT = 0x08;
static constexpr uint8_t NEGATIVE_ION_BIT = 0x40;
static constexpr uint8_t NIGHT_BIT = 0x80;
static constexpr uint8_t TURBO_FLAG = 0x10;
static constexpr uint8_t AIRFLOW_FLAG = 0x80;
static constexpr uint8_t ECO_TEMP_CODE = 0x08;

static constexpr uint8_t FIRST_PACKET_MARKER = 0x58;
static constexpr uint8_t SECOND_PACKET_MARKER = 0x78;

static constexpr uint32_t COUNTRYMOD_CARRIER_FREQUENCY = 38000;
static constexpr uint32_t COUNTRYMOD_HEADER_MARK_US = 9000;
static constexpr uint32_t COUNTRYMOD_HEADER_SPACE_US = 4500;
static constexpr uint32_t COUNTRYMOD_BIT_MARK_US = 560;
static constexpr uint32_t COUNTRYMOD_ONE_SPACE_US = 1690;
static constexpr uint32_t COUNTRYMOD_ZERO_SPACE_US = 520;
static constexpr uint32_t COUNTRYMOD_MESSAGE_SPACE_US = 20000;
static constexpr uint32_t COUNTRYMOD_FRAME_GAP_US = 25300;
static constexpr uint32_t COUNTRYMOD_PACKET_GAP_US = 40000;
static constexpr uint32_t COUNTRYMOD_MAX_PACKET_GAP_US = 120000;
static constexpr uint8_t COUNTRYMOD_TRAILER_BITS = 0b010;
static constexpr uint8_t COUNTRYMOD_TRAILER_NBITS = 3;
static constexpr uint8_t COUNTRYMOD_FRAME_NBITS = 32 + COUNTRYMOD_TRAILER_NBITS;
static constexpr uint8_t COUNTRYMOD_TAIL_NBITS = 32;
static constexpr uint8_t COUNTRYMOD_ECO_SECOND_TAIL_LOW_NIBBLE = 0x03;

static constexpr uint32_t LIGHT_COMMAND_FRAME = 0x008844CC;
static constexpr uint32_t DISPLAY_COMMAND_FRAME = 0x22AA66EE;
static constexpr uint32_t VIEW_VOLTAGE_COMMAND_FRAME = 0x55DD33BB;

static const char *const COUNTRYMOD_FAN_SPEED_1 = "Speed 1";
static const char *const COUNTRYMOD_FAN_SPEED_2 = "Speed 2";
static const char *const COUNTRYMOD_FAN_SPEED_3 = "Speed 3";
static const char *const COUNTRYMOD_FAN_SPEED_4 = "Speed 4";
static const char *const COUNTRYMOD_FAN_SPEED_5 = "Speed 5";
static const char *const COUNTRYMOD_CUSTOM_FAN_MODES[] = {COUNTRYMOD_FAN_SPEED_1, COUNTRYMOD_FAN_SPEED_2,
                                                          COUNTRYMOD_FAN_SPEED_3, COUNTRYMOD_FAN_SPEED_4,
                                                          COUNTRYMOD_FAN_SPEED_5};

void CountrymodClimate::setup() {
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 5, 0)
  this->set_supported_custom_fan_modes(COUNTRYMOD_CUSTOM_FAN_MODES);
#endif
  climate_ir::ClimateIR::setup();
  this->sanitize_state_();
  this->update_action_();
  this->publish_option_switches_();
}

void CountrymodClimate::dump_config() {
  LOG_CLIMATE("", "Countrymod Climate", this);
  ESP_LOGCONFIG(TAG, "  Climate packet gap: %" PRIu32 " us", this->packet_gap_us_());
  ESP_LOGCONFIG(TAG, "  Use power bit: %s", this->use_power_bit_ ? "yes" : "no");
  if (this->configured_packet_gap_us_() > COUNTRYMOD_MAX_PACKET_GAP_US) {
    ESP_LOGW(TAG, "  Configured inter_frame_delay is outside the captured range; using %" PRIu32 " us instead",
             COUNTRYMOD_PACKET_GAP_US);
  }
  if (this->feature_as_swing_) {
    ESP_LOGW(TAG, "  feature_as_swing is deprecated and ignored; use the negative_ion switch instead");
  }
  LOG_SWITCH("  ", "Turbo Switch", this->turbo_switch_);
  LOG_SWITCH("  ", "Night Switch", this->night_switch_);
  LOG_SWITCH("  ", "Negative Ion Switch", this->negative_ion_switch_);
  LOG_SWITCH("  ", "Eco Switch", this->eco_switch_);
  LOG_SWITCH("  ", "Airflow Switch", this->airflow_switch_);
}

climate::ClimateTraits CountrymodClimate::traits() {
  auto traits = climate::ClimateTraits();
  if (this->sensor_ != nullptr) {
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  }
  if (this->humidity_sensor_ != nullptr) {
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_HUMIDITY);
  }
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_FAN_ONLY});
  if (this->supports_cool_) {
    traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
  }
  if (this->supports_heat_) {
    traits.add_supported_mode(climate::CLIMATE_MODE_HEAT);
  }
  traits.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO});
#if ESPHOME_VERSION_CODE < VERSION_CODE(2026, 5, 0)
  traits.set_supported_custom_fan_modes(COUNTRYMOD_CUSTOM_FAN_MODES);
#endif
  traits.set_supported_presets(
      {climate::CLIMATE_PRESET_NONE, climate::CLIMATE_PRESET_ECO, climate::CLIMATE_PRESET_BOOST});
  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(30.0f);
  traits.set_visual_temperature_step(1.0f);
  return traits;
}

void CountrymodClimate::control(const climate::ClimateCall &call) {
  const bool preset_requested = call.get_preset().has_value();

  if (call.get_mode().has_value()) {
    auto requested_mode = *call.get_mode();
    switch (requested_mode) {
      case climate::CLIMATE_MODE_COOL:
        if (!this->supports_cool_) {
          ESP_LOGW(TAG, "Cool mode is disabled by configuration");
          break;
        }
        this->mode = requested_mode;
        this->last_on_mode_ = requested_mode;
        break;
      case climate::CLIMATE_MODE_HEAT:
        if (!this->supports_heat_) {
          ESP_LOGW(TAG, "Heat mode is disabled by configuration");
          break;
        }
        this->mode = requested_mode;
        this->last_on_mode_ = requested_mode;
        this->eco_on_ = false;
        break;
      case climate::CLIMATE_MODE_FAN_ONLY:
        this->mode = requested_mode;
        this->last_on_mode_ = requested_mode;
        this->eco_on_ = false;
        break;
      case climate::CLIMATE_MODE_OFF:
        this->mode = climate::CLIMATE_MODE_OFF;
        break;
      default:
        ESP_LOGW(TAG, "Unsupported mode requested: %s", LOG_STR_ARG(climate::climate_mode_to_string(requested_mode)));
        break;
    }
  }

  if (call.get_target_temperature().has_value()) {
    this->target_temperature = clamp<float>(std::round(*call.get_target_temperature()), 16.0f, 30.0f);
    if (this->eco_on_ && !preset_requested) {
      this->eco_on_ = false;
    }
  }

  if (call.get_fan_mode().has_value()) {
    switch (*call.get_fan_mode()) {
      case climate::CLIMATE_FAN_AUTO:
      case climate::CLIMATE_FAN_LOW:
      case climate::CLIMATE_FAN_MEDIUM:
      case climate::CLIMATE_FAN_HIGH:
        this->set_fan_mode_(*call.get_fan_mode());
        break;
      default:
        ESP_LOGW(TAG, "Unsupported fan mode requested: %s",
                 LOG_STR_ARG(climate::climate_fan_mode_to_string(*call.get_fan_mode())));
        break;
    }
  }
  if (call.has_custom_fan_mode()) {
    this->set_custom_fan_mode_(call.get_custom_fan_mode());
  }

  if (preset_requested) {
    switch (*call.get_preset()) {
      case climate::CLIMATE_PRESET_ECO:
        if (!this->supports_cool_) {
          ESP_LOGW(TAG, "Eco preset requires cool support");
          break;
        }
        this->eco_on_ = true;
        this->turbo_on_ = false;
        this->last_on_mode_ = climate::CLIMATE_MODE_COOL;
        if (this->mode != climate::CLIMATE_MODE_OFF) {
          this->mode = climate::CLIMATE_MODE_COOL;
        }
        break;
      case climate::CLIMATE_PRESET_BOOST:
        this->turbo_on_ = true;
        this->eco_on_ = false;
        break;
      case climate::CLIMATE_PRESET_NONE:
        this->eco_on_ = false;
        this->turbo_on_ = false;
        break;
      default:
        ESP_LOGW(TAG, "Unsupported preset requested: %s",
                 LOG_STR_ARG(climate::climate_preset_to_string(*call.get_preset())));
        break;
    }
  }

  this->sanitize_state_();
  this->update_action_();
  this->transmit_state();
  this->publish_state();
  this->publish_option_switches_();
}

bool CountrymodClimate::set_turbo(bool turbo_on) {
  this->turbo_on_ = turbo_on;
  if (this->turbo_on_) {
    this->eco_on_ = false;
  }
  this->update_preset_();
  this->transmit_state();
  this->publish_state();
  this->publish_option_switches_();
  return true;
}

bool CountrymodClimate::set_night(bool night_on) {
  this->night_on_ = night_on;
  this->transmit_state();
  this->publish_state();
  this->publish_option_switches_();
  return true;
}

bool CountrymodClimate::set_negative_ion(bool negative_ion_on) {
  this->negative_ion_on_ = negative_ion_on;
  this->transmit_state();
  this->publish_state();
  this->publish_option_switches_();
  return true;
}

bool CountrymodClimate::set_eco(bool eco_on) {
  if (eco_on && !this->supports_cool_) {
    ESP_LOGW(TAG, "Eco mode requires cool support");
    return false;
  }
  this->eco_on_ = eco_on;
  if (this->eco_on_) {
    this->turbo_on_ = false;
    this->last_on_mode_ = climate::CLIMATE_MODE_COOL;
    if (this->mode != climate::CLIMATE_MODE_OFF) {
      this->mode = climate::CLIMATE_MODE_COOL;
    }
  }
  this->sanitize_state_();
  this->update_action_();
  this->transmit_state();
  this->publish_state();
  this->publish_option_switches_();
  return true;
}

bool CountrymodClimate::set_airflow(bool airflow_on) {
  this->airflow_on_ = airflow_on;
  this->transmit_state();
  this->publish_state();
  this->publish_option_switches_();
  return true;
}

void CountrymodClimate::send_light_command() {
  ESP_LOGD(TAG, "Sending Countrymod light command: 0x%08" PRIX32, LIGHT_COMMAND_FRAME);
  this->transmit_countrymod_frame_(LIGHT_COMMAND_FRAME);
}

void CountrymodClimate::send_display_command() {
  ESP_LOGD(TAG, "Sending Countrymod display command: 0x%08" PRIX32, DISPLAY_COMMAND_FRAME);
  this->transmit_countrymod_frame_(DISPLAY_COMMAND_FRAME);
}

void CountrymodClimate::send_view_voltage_command() {
  ESP_LOGD(TAG, "Sending Countrymod view voltage command: 0x%08" PRIX32, VIEW_VOLTAGE_COMMAND_FRAME);
  this->transmit_countrymod_frame_(VIEW_VOLTAGE_COMMAND_FRAME);
}

void CountrymodClimate::transmit_state() {
  const uint32_t first_frame = this->make_frame_(false);
  const uint32_t second_frame = this->make_frame_(true);
  const uint32_t first_tail = this->make_tail_(false);
  const uint32_t second_tail = this->make_tail_(true);

  ESP_LOGD(TAG,
           "Sending Countrymod climate packet pair: 0x%08" PRIX32 "/0x%08" PRIX32 " then 0x%08" PRIX32
           "/0x%08" PRIX32 " (climate packet gap: %" PRIu32 " us)",
           first_frame, first_tail, second_frame, second_tail, this->packet_gap_us_());

  this->transmit_countrymod_climate_pair_(first_frame, first_tail, second_frame, second_tail);
}

bool CountrymodClimate::on_receive(remote_base::RemoteReceiveData data) {
  auto decoded = remote_base::LGProtocol().decode(data);
  if (!decoded.has_value() || decoded->nbits != 32) {
    return false;
  }
  if (decoded->data == LIGHT_COMMAND_FRAME) {
    ESP_LOGD(TAG, "Received Countrymod light command: 0x%08" PRIX32, decoded->data);
    return true;
  }
  if (decoded->data == DISPLAY_COMMAND_FRAME) {
    ESP_LOGD(TAG, "Received Countrymod display command: 0x%08" PRIX32, decoded->data);
    return true;
  }
  if (decoded->data == VIEW_VOLTAGE_COMMAND_FRAME) {
    ESP_LOGD(TAG, "Received Countrymod view voltage command: 0x%08" PRIX32, decoded->data);
    return true;
  }
  return this->apply_lg_frame_(decoded->data);
}

uint8_t CountrymodClimate::rev8_(uint8_t value) {
  value = (value & 0xF0) >> 4 | (value & 0x0F) << 4;
  value = (value & 0xCC) >> 2 | (value & 0x33) << 2;
  value = (value & 0xAA) >> 1 | (value & 0x55) << 1;
  return value;
}

uint32_t CountrymodClimate::make_frame_(bool second_packet) const {
  uint8_t state[8];
  this->build_state_(second_packet, state);

  return (uint32_t{rev8_(state[0])} << 24) | (uint32_t{rev8_(state[1])} << 16) |
         (uint32_t{rev8_(state[2])} << 8) | uint32_t{rev8_(state[3])};
}

uint32_t CountrymodClimate::make_tail_(bool second_packet) const {
  uint8_t state[8];
  this->build_state_(second_packet, state);

  return (uint32_t{rev8_(state[4])} << 24) | (uint32_t{rev8_(state[5])} << 16) |
         (uint32_t{rev8_(state[6])} << 8) | uint32_t{rev8_(state[7])};
}

void CountrymodClimate::build_state_(bool second_packet, uint8_t *state) const {
  uint8_t control = this->mode_base_for_transmit_();
  if (this->use_power_bit_ && this->mode != climate::CLIMATE_MODE_OFF) {
    control |= POWER_BIT;
  }
  control |= (this->fan_code_for_transmit_() & 0x03) << 4;
  if (this->negative_ion_on_) {
    control |= NEGATIVE_ION_BIT;
  }
  if (this->night_on_) {
    control |= NIGHT_BIT;
  }

  const auto temp_c = static_cast<uint8_t>(clamp<float>(std::round(this->target_temperature), 16.0f, 30.0f));
  const uint8_t temp_code = this->eco_on_ ? ECO_TEMP_CODE : temp_c - 16;
  uint8_t flags = 0x00;
  if (this->turbo_on_) {
    flags |= TURBO_FLAG;
  }
  if (this->airflow_on_) {
    flags |= AIRFLOW_FLAG;
  }
  const uint8_t marker = second_packet ? SECOND_PACKET_MARKER : FIRST_PACKET_MARKER;

  state[0] = control;
  state[1] = temp_code;
  state[2] = flags;
  state[3] = marker;
  state[4] = 0x00;
  state[5] = second_packet ? 0x00 : 0x20;
  state[6] = second_packet ? (this->tail_fan_code_for_transmit_() << 4) : 0x00;
  state[7] = second_packet && this->eco_on_ ? COUNTRYMOD_ECO_SECOND_TAIL_LOW_NIBBLE : 0x00;
  state[7] = (this->checksum_for_state_(state) << 4) | (state[7] & 0x0F);
}

uint8_t CountrymodClimate::checksum_for_state_(const uint8_t *state) const {
  uint8_t checksum = 0x0A;
  checksum += state[0] & 0x0F;
  checksum += state[1] & 0x0F;
  checksum += state[2] & 0x0F;
  checksum += state[3] & 0x0F;
  checksum += (state[5] & 0xF0) >> 4;
  checksum += (state[6] & 0xF0) >> 4;
  checksum += (state[7] & 0xF0) >> 4;
  return checksum & 0x0F;
}

void CountrymodClimate::transmit_countrymod_frame_(uint32_t frame) {
  if (this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "Cannot transmit without a remote transmitter");
    return;
  }

  auto transmit = this->transmitter_->transmit();
  this->encode_countrymod_frame_(transmit.get_data(), frame, COUNTRYMOD_FRAME_GAP_US);
  transmit.perform();
}

void CountrymodClimate::transmit_countrymod_climate_pair_(uint32_t first_frame, uint32_t first_tail,
                                                          uint32_t second_frame, uint32_t second_tail) {
  if (this->transmitter_ == nullptr) {
    ESP_LOGW(TAG, "Cannot transmit without a remote transmitter");
    return;
  }

  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();
  data->reserve((2 + COUNTRYMOD_FRAME_NBITS * 2u + 2 + COUNTRYMOD_TAIL_NBITS * 2u + 2) * 2u);
  this->encode_countrymod_climate_packet_(data, first_frame, first_tail, this->packet_gap_us_());
  this->encode_countrymod_climate_packet_(data, second_frame, second_tail, COUNTRYMOD_FRAME_GAP_US);
  transmit.perform();
}

void CountrymodClimate::encode_countrymod_frame_(remote_base::RemoteTransmitData *dst, uint32_t frame,
                                                 uint32_t gap_us) const {
  dst->set_carrier_frequency(COUNTRYMOD_CARRIER_FREQUENCY);
  dst->reserve(2 + COUNTRYMOD_FRAME_NBITS * 2u + 2);
  dst->item(COUNTRYMOD_HEADER_MARK_US, COUNTRYMOD_HEADER_SPACE_US);

  const uint64_t burst = (uint64_t{frame} << COUNTRYMOD_TRAILER_NBITS) | COUNTRYMOD_TRAILER_BITS;
  for (uint64_t mask = uint64_t{1} << (COUNTRYMOD_FRAME_NBITS - 1); mask != 0; mask >>= 1) {
    dst->item(COUNTRYMOD_BIT_MARK_US, (burst & mask) != 0 ? COUNTRYMOD_ONE_SPACE_US : COUNTRYMOD_ZERO_SPACE_US);
  }
  dst->mark(COUNTRYMOD_BIT_MARK_US);
  dst->space(gap_us);
}

void CountrymodClimate::encode_countrymod_tail_(remote_base::RemoteTransmitData *dst, uint32_t tail,
                                                uint32_t gap_us) const {
  for (uint32_t mask = uint32_t{1} << (COUNTRYMOD_TAIL_NBITS - 1); mask != 0; mask >>= 1) {
    dst->item(COUNTRYMOD_BIT_MARK_US, (tail & mask) != 0 ? COUNTRYMOD_ONE_SPACE_US : COUNTRYMOD_ZERO_SPACE_US);
  }
  dst->mark(COUNTRYMOD_BIT_MARK_US);
  dst->space(gap_us);
}

void CountrymodClimate::encode_countrymod_climate_packet_(remote_base::RemoteTransmitData *dst, uint32_t frame,
                                                          uint32_t tail, uint32_t final_gap_us) const {
  this->encode_countrymod_frame_(dst, frame, COUNTRYMOD_MESSAGE_SPACE_US);
  this->encode_countrymod_tail_(dst, tail, final_gap_us);
}

uint32_t CountrymodClimate::configured_packet_gap_us_() const { return this->inter_frame_delay_ms_ * 1000UL; }

uint32_t CountrymodClimate::packet_gap_us_() const {
  const uint32_t configured_gap_us = this->configured_packet_gap_us_();
  if (configured_gap_us == 7000 || configured_gap_us > COUNTRYMOD_MAX_PACKET_GAP_US) {
    return COUNTRYMOD_PACKET_GAP_US;
  }
  return configured_gap_us;
}

bool CountrymodClimate::apply_lg_frame_(uint32_t frame) {
  const uint8_t control = rev8_((frame >> 24) & 0xFF);
  const uint8_t temp_code = rev8_((frame >> 16) & 0xFF);
  const uint8_t flags = rev8_((frame >> 8) & 0xFF);
  const uint8_t marker = rev8_(frame & 0xFF);

  if (marker != FIRST_PACKET_MARKER && marker != SECOND_PACKET_MARKER) {
    return false;
  }

  const uint8_t mode_base = control & 0x07;
  const bool decoded_eco = mode_base == MODE_BASE_ECO;
  const climate::ClimateMode decoded_mode = decoded_eco ? climate::CLIMATE_MODE_COOL : this->mode_from_base_(mode_base);
  if (decoded_mode == climate::CLIMATE_MODE_OFF) {
    ESP_LOGV(TAG, "Ignoring Countrymod frame with unknown mode base 0x%02X", mode_base);
    return false;
  }
  if ((decoded_mode == climate::CLIMATE_MODE_COOL && !this->supports_cool_) ||
      (decoded_mode == climate::CLIMATE_MODE_HEAT && !this->supports_heat_)) {
    ESP_LOGV(TAG, "Ignoring Countrymod frame with disabled mode %s",
             LOG_STR_ARG(climate::climate_mode_to_string(decoded_mode)));
    return false;
  }

  if (!this->use_power_bit_ || (control & POWER_BIT) != 0) {
    this->mode = decoded_mode;
    this->last_on_mode_ = decoded_mode;
  } else {
    this->mode = climate::CLIMATE_MODE_OFF;
    this->last_on_mode_ = decoded_mode;
  }

  this->set_fan_mode_(this->fan_mode_from_code_((control >> 4) & 0x03));
  if (!decoded_eco) {
    this->target_temperature = clamp<float>(static_cast<float>(temp_code) + 16.0f, 16.0f, 30.0f);
  }
  this->negative_ion_on_ = (control & NEGATIVE_ION_BIT) != 0;
  this->night_on_ = (control & NIGHT_BIT) != 0;
  this->turbo_on_ = (flags & TURBO_FLAG) != 0;
  this->eco_on_ = decoded_eco && !this->turbo_on_;
  this->airflow_on_ = (flags & AIRFLOW_FLAG) != 0;

  this->sanitize_state_();
  this->update_action_();
  ESP_LOGD(TAG, "Decoded Countrymod frame 0x%08" PRIX32, frame);
  this->publish_state();
  this->publish_option_switches_();
  return true;
}

climate::ClimateMode CountrymodClimate::mode_from_base_(uint8_t mode_base) const {
  switch (mode_base) {
    case MODE_BASE_COOL:
      return climate::CLIMATE_MODE_COOL;
    case MODE_BASE_HEAT:
      return climate::CLIMATE_MODE_HEAT;
    case MODE_BASE_FAN:
      return climate::CLIMATE_MODE_FAN_ONLY;
    default:
      return climate::CLIMATE_MODE_OFF;
  }
}

uint8_t CountrymodClimate::mode_base_for_transmit_() const {
  if (this->eco_on_) {
    return MODE_BASE_ECO;
  }

  climate::ClimateMode effective_mode = this->mode == climate::CLIMATE_MODE_OFF ? this->last_on_mode_ : this->mode;
  switch (effective_mode) {
    case climate::CLIMATE_MODE_HEAT:
      return MODE_BASE_HEAT;
    case climate::CLIMATE_MODE_FAN_ONLY:
      return MODE_BASE_FAN;
    case climate::CLIMATE_MODE_COOL:
    default:
      return MODE_BASE_COOL;
  }
}

uint8_t CountrymodClimate::fan_speed_for_transmit_() const {
  if (this->turbo_on_) {
    return 5;
  }
  if (this->has_custom_fan_mode()) {
    auto custom_fan_mode = this->get_custom_fan_mode();
    if (custom_fan_mode == COUNTRYMOD_FAN_SPEED_1) {
      return 1;
    }
    if (custom_fan_mode == COUNTRYMOD_FAN_SPEED_2) {
      return 2;
    }
    if (custom_fan_mode == COUNTRYMOD_FAN_SPEED_3) {
      return 3;
    }
    if (custom_fan_mode == COUNTRYMOD_FAN_SPEED_4) {
      return 4;
    }
    if (custom_fan_mode == COUNTRYMOD_FAN_SPEED_5) {
      return 5;
    }
  }
  if (!this->fan_mode.has_value()) {
    return 0;
  }
  switch (*this->fan_mode) {
    case climate::CLIMATE_FAN_LOW:
      return 1;
    case climate::CLIMATE_FAN_MEDIUM:
      return 2;
    case climate::CLIMATE_FAN_HIGH:
      return 5;
    case climate::CLIMATE_FAN_AUTO:
    default:
      return 0;
  }
}

uint8_t CountrymodClimate::fan_code_for_transmit_() const {
  return std::min<uint8_t>(this->fan_speed_for_transmit_(), 3);
}

uint8_t CountrymodClimate::tail_fan_code_for_transmit_() const {
  return this->fan_speed_for_transmit_();
}

climate::ClimateFanMode CountrymodClimate::fan_mode_from_code_(uint8_t fan_code) const {
  switch (fan_code & 0x03) {
    case 1:
      return climate::CLIMATE_FAN_LOW;
    case 2:
      return climate::CLIMATE_FAN_MEDIUM;
    case 3:
      return climate::CLIMATE_FAN_HIGH;
    case 0:
    default:
      return climate::CLIMATE_FAN_AUTO;
  }
}

void CountrymodClimate::publish_option_switches_() {
  this->publish_option_switch_(this->turbo_switch_, this->turbo_on_);
  this->publish_option_switch_(this->night_switch_, this->night_on_);
  this->publish_option_switch_(this->negative_ion_switch_, this->negative_ion_on_);
  this->publish_option_switch_(this->eco_switch_, this->eco_on_);
  this->publish_option_switch_(this->airflow_switch_, this->airflow_on_);
}

void CountrymodClimate::publish_option_switch_(switch_::Switch *option_switch, bool state) {
  if (option_switch != nullptr) {
    option_switch->publish_state(state);
  }
}

void CountrymodClimate::update_action_() {
  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
      this->action = climate::CLIMATE_ACTION_COOLING;
      break;
    case climate::CLIMATE_MODE_HEAT:
      this->action = climate::CLIMATE_ACTION_HEATING;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      this->action = climate::CLIMATE_ACTION_FAN;
      break;
    case climate::CLIMATE_MODE_OFF:
    default:
      this->action = climate::CLIMATE_ACTION_OFF;
      break;
  }
}

void CountrymodClimate::update_preset_() {
  if (this->eco_on_) {
    this->set_preset_(climate::CLIMATE_PRESET_ECO);
  } else if (this->turbo_on_) {
    this->set_preset_(climate::CLIMATE_PRESET_BOOST);
  } else {
    this->set_preset_(climate::CLIMATE_PRESET_NONE);
  }
}

void CountrymodClimate::sanitize_state_() {
  if ((this->last_on_mode_ == climate::CLIMATE_MODE_COOL && !this->supports_cool_) ||
      (this->last_on_mode_ == climate::CLIMATE_MODE_HEAT && !this->supports_heat_)) {
    if (this->supports_cool_) {
      this->last_on_mode_ = climate::CLIMATE_MODE_COOL;
    } else if (this->supports_heat_) {
      this->last_on_mode_ = climate::CLIMATE_MODE_HEAT;
    } else {
      this->last_on_mode_ = climate::CLIMATE_MODE_FAN_ONLY;
    }
  }

  switch (this->mode) {
    case climate::CLIMATE_MODE_COOL:
      if (!this->supports_cool_) {
        this->mode = climate::CLIMATE_MODE_OFF;
        break;
      }
      this->last_on_mode_ = this->mode;
      break;
    case climate::CLIMATE_MODE_HEAT:
      if (!this->supports_heat_) {
        this->mode = climate::CLIMATE_MODE_OFF;
        break;
      }
      this->last_on_mode_ = this->mode;
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      this->last_on_mode_ = this->mode;
      break;
    case climate::CLIMATE_MODE_OFF:
      break;
    default:
      this->mode = climate::CLIMATE_MODE_OFF;
      break;
  }

  if (!this->fan_mode.has_value()) {
    if (!this->has_custom_fan_mode()) {
      this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
    }
  } else {
    switch (*this->fan_mode) {
      case climate::CLIMATE_FAN_AUTO:
      case climate::CLIMATE_FAN_LOW:
      case climate::CLIMATE_FAN_MEDIUM:
      case climate::CLIMATE_FAN_HIGH:
        break;
      default:
        this->set_fan_mode_(climate::CLIMATE_FAN_AUTO);
        break;
    }
  }

  if (std::isnan(this->target_temperature)) {
    this->target_temperature = 22.0f;
  } else {
    this->target_temperature = clamp<float>(std::round(this->target_temperature), 16.0f, 30.0f);
  }

  this->swing_mode = climate::CLIMATE_SWING_OFF;

  if (this->eco_on_ && !this->supports_cool_) {
    this->eco_on_ = false;
  }
  if (this->eco_on_) {
    this->turbo_on_ = false;
    this->last_on_mode_ = climate::CLIMATE_MODE_COOL;
    if (this->mode != climate::CLIMATE_MODE_OFF) {
      this->mode = climate::CLIMATE_MODE_COOL;
    }
  }
  this->update_preset_();
}

void CountrymodButton::press_action() {
  if (this->get_parent() == nullptr) {
    ESP_LOGW(TAG, "Ignoring button press without parent");
    return;
  }

  switch (this->kind_) {
    case COUNTRYMOD_BUTTON_LIGHT:
      this->get_parent()->send_light_command();
      break;
    case COUNTRYMOD_BUTTON_DISPLAY:
      this->get_parent()->send_display_command();
      break;
    case COUNTRYMOD_BUTTON_VIEW_VOLTAGE:
      this->get_parent()->send_view_voltage_command();
      break;
  }
}

void CountrymodSwitch::write_state(bool state) {
  if (this->get_parent() == nullptr) {
    ESP_LOGW(TAG, "Ignoring switch write without parent");
    return;
  }

  bool ok = false;
  switch (this->kind_) {
    case COUNTRYMOD_SWITCH_TURBO:
      ok = this->get_parent()->set_turbo(state);
      break;
    case COUNTRYMOD_SWITCH_NIGHT:
      ok = this->get_parent()->set_night(state);
      break;
    case COUNTRYMOD_SWITCH_NEGATIVE_ION:
      ok = this->get_parent()->set_negative_ion(state);
      break;
    case COUNTRYMOD_SWITCH_ECO:
      ok = this->get_parent()->set_eco(state);
      break;
    case COUNTRYMOD_SWITCH_AIRFLOW:
      ok = this->get_parent()->set_airflow(state);
      break;
  }

  if (!ok) {
    ESP_LOGW(TAG, "Failed to write Countrymod switch state");
  }
}

}  // namespace esphome::countrymod

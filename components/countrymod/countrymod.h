#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"

#include <cinttypes>

namespace esphome::countrymod {

enum CountrymodSwitchKind : uint8_t {
  COUNTRYMOD_SWITCH_TURBO = 0,
  COUNTRYMOD_SWITCH_NIGHT = 1,
  COUNTRYMOD_SWITCH_NEGATIVE_ION = 2,
  COUNTRYMOD_SWITCH_FEATURE = COUNTRYMOD_SWITCH_NEGATIVE_ION,
  COUNTRYMOD_SWITCH_ECO = 3,
  COUNTRYMOD_SWITCH_AIRFLOW = 4,
};

enum CountrymodButtonKind : uint8_t {
  COUNTRYMOD_BUTTON_LIGHT = 0,
  COUNTRYMOD_BUTTON_DISPLAY = 1,
  COUNTRYMOD_BUTTON_VIEW_VOLTAGE = 2,
  COUNTRYMOD_BUTTON_ZIGZAG = COUNTRYMOD_BUTTON_VIEW_VOLTAGE,
};

enum CountrymodPresetMode : uint8_t {
  COUNTRYMOD_PRESET_AUTO = 0,
  COUNTRYMOD_PRESET_ECO = 1,
  COUNTRYMOD_PRESET_TURBO = 2,
};

class CountrymodClimate : public climate_ir::ClimateIR {
 public:
  CountrymodClimate()
      : climate_ir::ClimateIR(16.0f, 30.0f, 1.0f, false, true, {climate::CLIMATE_FAN_AUTO}) {}

  void setup() override;
  void dump_config() override;

  void set_feature_as_swing(bool feature_as_swing) { this->feature_as_swing_ = feature_as_swing; }
  void set_inter_frame_delay(uint32_t inter_frame_delay_ms) { this->inter_frame_delay_ms_ = inter_frame_delay_ms; }
  void set_use_power_bit(bool use_power_bit) { this->use_power_bit_ = use_power_bit; }

  bool set_preset_mode(size_t mode_index);
  bool set_turbo(bool turbo_on);
  bool set_night(bool night_on);
  bool set_negative_ion(bool negative_ion_on);
  bool set_feature(bool feature_on) { return this->set_negative_ion(feature_on); }
  bool set_eco(bool eco_on);
  bool set_airflow(bool airflow_on);
  void send_light_command();
  void send_display_command();
  void send_view_voltage_command();
  void send_zigzag_command() { this->send_view_voltage_command(); }

  void set_mode_select(select::Select *mode_select) { this->mode_select_ = mode_select; }
  void set_turbo_switch(switch_::Switch *turbo_switch) { this->turbo_switch_ = turbo_switch; }
  void set_night_switch(switch_::Switch *night_switch) { this->night_switch_ = night_switch; }
  void set_negative_ion_switch(switch_::Switch *negative_ion_switch) { this->negative_ion_switch_ = negative_ion_switch; }
  void set_feature_switch(switch_::Switch *feature_switch) { this->set_negative_ion_switch(feature_switch); }
  void set_eco_switch(switch_::Switch *eco_switch) { this->eco_switch_ = eco_switch; }
  void set_airflow_switch(switch_::Switch *airflow_switch) { this->airflow_switch_ = airflow_switch; }

 protected:
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

  static uint8_t rev8_(uint8_t value);

  uint32_t make_frame_(bool second_packet) const;
  uint32_t make_tail_(bool second_packet) const;
  void build_state_(bool second_packet, uint8_t *state) const;
  uint8_t checksum_for_state_(const uint8_t *state) const;
  void transmit_countrymod_frame_(uint32_t frame);
  void transmit_countrymod_climate_pair_(uint32_t first_frame, uint32_t first_tail, uint32_t second_frame,
                                         uint32_t second_tail);
  void encode_countrymod_frame_(remote_base::RemoteTransmitData *dst, uint32_t frame, uint32_t gap_us) const;
  void encode_countrymod_tail_(remote_base::RemoteTransmitData *dst, uint32_t tail, uint32_t gap_us) const;
  void encode_countrymod_climate_packet_(remote_base::RemoteTransmitData *dst, uint32_t frame, uint32_t tail,
                                         uint32_t final_gap_us) const;
  uint32_t configured_packet_gap_us_() const;
  uint32_t packet_gap_us_() const;
  bool apply_lg_frame_(uint32_t frame);

  climate::ClimateMode mode_from_base_(uint8_t mode_base) const;
  uint8_t mode_base_for_transmit_() const;
  uint8_t fan_speed_for_transmit_() const;
  uint8_t fan_code_for_transmit_() const;
  uint8_t tail_fan_code_for_transmit_() const;
  climate::ClimateFanMode fan_mode_from_code_(uint8_t fan_code) const;

  void publish_option_switches_();
  void publish_option_switch_(switch_::Switch *option_switch, bool state);
  void publish_mode_select_();
  void update_action_();
  void update_preset_();
  void sanitize_state_();

  climate::ClimateMode last_on_mode_{climate::CLIMATE_MODE_COOL};
  bool turbo_on_{false};
  bool night_on_{false};
  bool negative_ion_on_{false};
  bool eco_on_{false};
  bool airflow_on_{false};
  bool feature_as_swing_{false};
  bool use_power_bit_{true};
  uint32_t inter_frame_delay_ms_{40};

  select::Select *mode_select_{nullptr};
  switch_::Switch *turbo_switch_{nullptr};
  switch_::Switch *night_switch_{nullptr};
  switch_::Switch *negative_ion_switch_{nullptr};
  switch_::Switch *eco_switch_{nullptr};
  switch_::Switch *airflow_switch_{nullptr};
};

class CountrymodButton : public button::Button, public Parented<CountrymodClimate> {
 public:
  explicit CountrymodButton(CountrymodButtonKind kind) : kind_(kind) {}

 protected:
  void press_action() override;

  CountrymodButtonKind kind_;
};

class CountrymodModeSelect : public select::Select, public Parented<CountrymodClimate> {
 protected:
  void control(size_t index) override;
};

class CountrymodSwitch : public switch_::Switch, public Parented<CountrymodClimate> {
 public:
  explicit CountrymodSwitch(CountrymodSwitchKind kind) : kind_(kind) {}

 protected:
  void write_state(bool state) override;

  CountrymodSwitchKind kind_;
};

}  // namespace esphome::countrymod

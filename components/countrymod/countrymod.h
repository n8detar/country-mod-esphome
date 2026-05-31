#pragma once

#include "esphome/components/button/button.h"
#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/switch/switch.h"

#include <cinttypes>

namespace esphome::countrymod {

enum CountrymodSwitchKind : uint8_t {
  COUNTRYMOD_SWITCH_TURBO = 0,
  COUNTRYMOD_SWITCH_NIGHT = 1,
  COUNTRYMOD_SWITCH_FEATURE = 2,
};

enum CountrymodButtonKind : uint8_t {
  COUNTRYMOD_BUTTON_LIGHT = 0,
};

class CountrymodClimate : public climate_ir::ClimateIR {
 public:
  CountrymodClimate()
      : climate_ir::ClimateIR(16.0f, 30.0f, 1.0f, false, true,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
                               climate::CLIMATE_FAN_HIGH}) {}

  void setup() override;
  void dump_config() override;

  void set_feature_as_swing(bool feature_as_swing) { this->feature_as_swing_ = feature_as_swing; }
  void set_inter_frame_delay(uint32_t inter_frame_delay_ms) { this->inter_frame_delay_ms_ = inter_frame_delay_ms; }

  bool set_turbo(bool turbo_on);
  bool set_night(bool night_on);
  bool set_feature(bool feature_on);
  void send_light_command();

  void set_turbo_switch(switch_::Switch *turbo_switch) { this->turbo_switch_ = turbo_switch; }
  void set_night_switch(switch_::Switch *night_switch) { this->night_switch_ = night_switch; }
  void set_feature_switch(switch_::Switch *feature_switch) { this->feature_switch_ = feature_switch; }

 protected:
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

  static uint8_t rev8_(uint8_t value);

  uint32_t make_frame_(bool second_packet) const;
  void transmit_lg_frame_(uint32_t frame);
  bool apply_lg_frame_(uint32_t frame);

  climate::ClimateMode mode_from_base_(uint8_t mode_base) const;
  uint8_t mode_base_for_transmit_() const;
  uint8_t fan_code_for_transmit_() const;
  climate::ClimateFanMode fan_mode_from_code_(uint8_t fan_code) const;

  void publish_option_switches_();
  void publish_option_switch_(switch_::Switch *option_switch, bool state);
  void update_action_();
  void sanitize_state_();

  climate::ClimateMode last_on_mode_{climate::CLIMATE_MODE_COOL};
  bool turbo_on_{false};
  bool night_on_{false};
  bool feature_on_{false};
  bool feature_as_swing_{false};
  uint32_t inter_frame_delay_ms_{110};

  switch_::Switch *turbo_switch_{nullptr};
  switch_::Switch *night_switch_{nullptr};
  switch_::Switch *feature_switch_{nullptr};
};

class CountrymodButton : public button::Button, public Parented<CountrymodClimate> {
 public:
  explicit CountrymodButton(CountrymodButtonKind kind) : kind_(kind) {}

 protected:
  void press_action() override;

  CountrymodButtonKind kind_;
};

class CountrymodSwitch : public switch_::Switch, public Parented<CountrymodClimate> {
 public:
  explicit CountrymodSwitch(CountrymodSwitchKind kind) : kind_(kind) {}

 protected:
  void write_state(bool state) override;

  CountrymodSwitchKind kind_;
};

}  // namespace esphome::countrymod

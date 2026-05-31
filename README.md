# Countrymod RV AC ESPHome Component

ESPHome external component for the Countrymod RV AC IR protocol decoded from
`remote.lg` receiver captures.

This project is entirely vibe-coded and provided as-is. Use it at your own risk.

The protocol is full-state: each update sends two state packets. The component
keeps the current climate state locally, builds both packet variants, and
transmits the pair as one raw IR sequence.

Climate packets are much longer than the one-off display and voltage commands.
The example uses `rmt_symbols: 192`, `non_blocking: false`, and a 190 ms packet
spacing because the captured handheld Cool/72F/Auto ON command used about that
spacing between the two full-state packets.

ESPHome's receiver identifies the first 32 bits as `remote.lg`, but the physical
remote transmits those bits as a Countrymod burst with a 9 ms / 4.5 ms header,
a fixed `010` separator, a second 32-bit tail block, and the captured
Countrymod timing rather than ESPHome's generic LG timing.

## Example

```yaml
esphome:
  name: countrymod-rv-ac
  friendly_name: Countrymod RV AC
  devices:
    - id: rv_ac_device
      name: RV AC

external_components:
  - source: github://n8detar/country-mod-esphome@main
    components: [countrymod]

remote_transmitter:
  id: ir_tx
  pin: GPIO4
  carrier_duty_percent: 50%
  rmt_symbols: 192
  non_blocking: false

climate:
  - platform: countrymod
    id: rv_ac
    name: Climate
    device_id: rv_ac_device
    transmitter_id: ir_tx
    supports_heat: false
    inter_frame_delay: 190ms
    use_power_bit: true

switch:
  - platform: countrymod
    countrymod_id: rv_ac
    type: turbo
    name: Turbo
    device_id: rv_ac_device

  - platform: countrymod
    countrymod_id: rv_ac
    type: night
    name: Night
    device_id: rv_ac_device

  - platform: countrymod
    countrymod_id: rv_ac
    type: negative_ion
    name: Negative Ion
    device_id: rv_ac_device
    disabled_by_default: true

  - platform: countrymod
    countrymod_id: rv_ac
    type: eco
    name: Eco
    device_id: rv_ac_device

  - platform: countrymod
    countrymod_id: rv_ac
    type: auxiliary
    name: Auxiliary Function
    device_id: rv_ac_device
    disabled_by_default: true

button:
  - platform: countrymod
    countrymod_id: rv_ac
    type: display
    name: Display
    device_id: rv_ac_device

  - platform: countrymod
    countrymod_id: rv_ac
    type: view_voltage
    name: View Voltage
    device_id: rv_ac_device

  - platform: countrymod
    countrymod_id: rv_ac
    type: light
    name: Light
    device_id: rv_ac_device
    disabled_by_default: true
```

The `device_id` entries are optional, but when present they put the climate,
switches, and buttons under the `RV AC` sub-device in Home Assistant instead of
the ESP controller device.

Supported climate modes are `OFF`, `COOL`, `HEAT`, and `FAN_ONLY`. Set
`supports_heat: false` to hide heat mode and ignore received heat frames on
cool-only units. Standard fan modes are `AUTO`, `LOW`, `MEDIUM`, and `HIGH`;
`AUTO`, `LOW`, `MEDIUM`, and `HIGH` map to captured auto, speed 1, speed 2, and
speed 5 fields. Custom fan modes `Speed 1` through `Speed 5` are also exposed.
The first 32-bit decode uses the protocol's shared code for display speeds
3/4/5, while the hidden 32-bit tail selects the exact fan speed.

The remote's main `ECO`, `AUTO`, and `TURBO` modes are exposed through standard
climate presets as `ECO`, `NONE`, and `BOOST`. The optional `eco` and `turbo`
switches provide direct switch entities for the same protocol state. `BOOST`
transmits the turbo flag with the captured max-fan IR fields.

`use_power_bit: true` matches handheld captures where ON climate frames decode
as `0x9060...` and matching OFF frames decode as `0x8060...`. If your remote
never uses the `0x08` control bit, set `use_power_bit: false`.

Protocol bit `0x40` is exposed as `negative_ion`, matching the manual. The older
`feature` switch type remains accepted as an alias. `feature_as_swing` is still
accepted for old configs but is ignored because the manual does not list swing as
an IR function.

The `display` button sends the `DISP.` screen-display command. The
`view_voltage` button sends the command labeled View Voltage; the older `zigzag`
button type remains accepted as an alias.

The crossed-out lightbulb button and the horizontal-line/three-squiggle full-state
flag do not appear to do anything on this model. Omit those entities entirely, or
include `disabled_by_default: true` as shown above. The unused full-state flag is
available as `auxiliary`; the older `airflow` switch type remains accepted as an
alias.

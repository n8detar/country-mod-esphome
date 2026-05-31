# Countrymod RV AC ESPHome Component

ESPHome external component for the Countrymod RV AC IR protocol decoded from
`remote.lg` receiver captures.

The protocol is full-state: each update sends two 32-bit LG-style frames. The
component keeps the current climate state locally, builds both packet variants,
and transmits them about 110 ms apart.

## Example

```yaml
external_components:
  - source: github://n8detar/country-mod-esphome@main
    components: [countrymod]

remote_transmitter:
  id: ir_tx
  pin: GPIO4
  carrier_duty_percent: 50%
  non_blocking: true

climate:
  - platform: countrymod
    id: rv_ac
    name: RV AC
    transmitter_id: ir_tx
    supports_heat: false
    inter_frame_delay: 110ms

switch:
  - platform: countrymod
    countrymod_id: rv_ac
    type: turbo
    name: RV AC Turbo

  - platform: countrymod
    countrymod_id: rv_ac
    type: night
    name: RV AC Night

  - platform: countrymod
    countrymod_id: rv_ac
    type: negative_ion
    name: RV AC Negative Ion
    disabled_by_default: true

  - platform: countrymod
    countrymod_id: rv_ac
    type: eco
    name: RV AC Eco

  - platform: countrymod
    countrymod_id: rv_ac
    type: auxiliary
    name: RV AC Auxiliary Function
    disabled_by_default: true

button:
  - platform: countrymod
    countrymod_id: rv_ac
    type: display
    name: RV AC Display

  - platform: countrymod
    countrymod_id: rv_ac
    type: view_voltage
    name: RV AC View Voltage

  - platform: countrymod
    countrymod_id: rv_ac
    type: light
    name: RV AC Light
    disabled_by_default: true
```

Supported climate modes are `OFF`, `COOL`, `HEAT`, and `FAN_ONLY`. Set
`supports_heat: false` to hide heat mode and ignore received heat frames on
cool-only units. Supported fan modes are `AUTO`, `LOW`, `MEDIUM`, and `HIGH`,
where `HIGH` maps to the protocol's shared fan code for display speeds 3/4/5.

The remote's main `ECO`, `AUTO`, and `TURBO` modes are exposed through standard
climate presets as `ECO`, `NONE`, and `BOOST`. The optional `eco` and `turbo`
switches provide direct switch entities for the same protocol state.

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

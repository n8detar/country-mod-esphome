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
    # Optional: only enable after confirming bit 0x40 is actually swing.
    feature_as_swing: false

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
    type: feature
    name: RV AC Feature Bit

  - platform: countrymod
    countrymod_id: rv_ac
    type: eco
    name: RV AC Eco

  - platform: countrymod
    countrymod_id: rv_ac
    type: airflow
    name: RV AC Airflow Bit

button:
  - platform: countrymod
    countrymod_id: rv_ac
    type: light
    name: RV AC Light

  - platform: countrymod
    countrymod_id: rv_ac
    type: display
    name: RV AC Display

  - platform: countrymod
    countrymod_id: rv_ac
    type: zigzag
    name: RV AC Zigzag
```

Supported climate modes are `OFF`, `COOL`, `HEAT`, and `FAN_ONLY`. Supported
fan modes are `AUTO`, `LOW`, `MEDIUM`, and `HIGH`, where `HIGH` maps to the
protocol's shared fan code for display speeds 3/4/5.

The remote's main `ECO`, `AUTO`, and `TURBO` modes are exposed through standard
climate presets as `ECO`, `NONE`, and `BOOST`. The optional `eco` and `turbo`
switches provide direct switch entities for the same protocol state.

The ambiguous protocol bit `0x40` is exposed as a neutral `feature` switch by
default. Set `feature_as_swing: true` only after verifying that the physical
unit treats that bit as swing.

The `airflow` switch sends the full-state flag captured from the remote button
labeled with a horizontal line and three rising squiggles. Its exact physical
behavior still needs verification, so the name intentionally stays generic.

The `light`, `display`, and `zigzag` buttons send separate one-frame commands
captured from the crossed-out lightbulb, `DISP.`, and three-section zig-zag
remote labels. Their exact physical behavior still needs verification, so they
are exposed as momentary buttons rather than switches.

# Countrymod RV AC ESPHome Component

ESPHome external component for Countrymod RV air conditioners that use the
YAP0F/YAP0F3 handheld IR remote protocol.

This project is vibe-coded and provided as-is. Use it at your own risk.

The protocol is full-state. A normal climate command sends two complete state
packets, not a short button command. ESPHome's generic receiver often reports
the first 32 bits as `remote.lg`, but this component transmits and receives the
complete Countrymod packet structure, including trailer bits, hidden tail data,
checksum, and captured packet timing.

## Example

```yaml
esphome:
  name: countrymod-rv-ac
  friendly_name: Countrymod RV AC

esp32:
  board: esp32dev
  framework:
    type: esp-idf

logger:

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
    transmitter_id: ir_tx
    supports_heat: false

select:
  - platform: countrymod
    countrymod_id: rv_ac
    name: Mode

switch:
  - platform: countrymod
    countrymod_id: rv_ac
    type: night
    name: Night

  - platform: countrymod
    countrymod_id: rv_ac
    type: negative_ion
    name: Negative Ion

button:
  - platform: countrymod
    countrymod_id: rv_ac
    type: display
    name: Display

  - platform: countrymod
    countrymod_id: rv_ac
    type: view_voltage
    name: View Voltage
```

`rmt_symbols: 192` is recommended because each climate transmit contains two
long full-state packets. `non_blocking: false` keeps the packet pair together as
one IR transmit sequence.

Set `supports_heat: false` for cool-only units. Omit it, or set it to `true`, if
your unit responds to heat commands.

## Entities

| Platform | Type | Purpose |
| --- | --- | --- |
| `climate.countrymod` | `Climate` | Power, HVAC mode, target temperature, and fan speed. |
| `select.countrymod` | `Mode` | Remote mode selector: `Auto`, `Eco`, or `Turbo`. |
| `switch.countrymod` | `night` | Night/sleep flag. |
| `switch.countrymod` | `negative_ion` | Negative Ion flag from the remote/manual. |
| `button.countrymod` | `display` | Sends the `DISP.` screen-display command. |
| `button.countrymod` | `view_voltage` | Sends the View Voltage command. |
| `button.countrymod` | `light` | Sends the crossed-out lightbulb command. |

The `Mode` select is the canonical control for the remote's `AUTO`, `ECO`, and
`TURBO` buttons. `Auto` means both Eco and Turbo protocol flags are off; it is
not a separate climate auto HVAC mode. Eco and Turbo are mutually exclusive.

Fan control exposes standard `Auto` plus custom fan speeds `Speed 1` through
`Speed 5`, matching the handheld remote. Fan speeds 3, 4, and 5 share the same
first-frame display fan code; the second packet tail stores the exact speed.

Home Assistant can show Fahrenheit while ESPHome stores temperatures in Celsius.
The protocol range is 16-30 C, equivalent to roughly 61-86 F.

## Configuration

### Climate

```yaml
climate:
  - platform: countrymod
    id: rv_ac
    name: Climate
    transmitter_id: ir_tx
```

| Option | Default | Description |
| --- | --- | --- |
| `transmitter_id` | required | Remote transmitter used for IR output. |
| `receiver_id` | optional | Remote receiver used to observe handheld remote packets. |
| `supports_cool` | `true` | Exposes cool mode and accepts cool frames. |
| `supports_heat` | `true` | Exposes heat mode and accepts heat frames. |
| `inter_frame_delay` | `40ms` | Gap between the two full-state climate packets. |
| `use_power_bit` | `true` | Uses protocol bit `0x08` to distinguish on/off climate frames. |

`inter_frame_delay: 40ms` matches the captured YAP0F/YAP0F3 remote behavior.

### Mode Select

```yaml
select:
  - platform: countrymod
    countrymod_id: rv_ac
    name: Mode
```

Options are `Auto`, `Eco`, and `Turbo`. `Auto` is the default state at startup
unless a received packet enables Eco or Turbo.

### Switches

```yaml
switch:
  - platform: countrymod
    countrymod_id: rv_ac
    type: night
    name: Night
```

Supported switch types are:

| Type | Description |
| --- | --- |
| `night` | Night/sleep flag. |
| `negative_ion` | Negative Ion flag. |

### Buttons

```yaml
button:
  - platform: countrymod
    countrymod_id: rv_ac
    type: display
    name: Display
```

Supported button types are:

| Type | Command frame | Description |
| --- | --- | --- |
| `display` | `0x22AA66EE` | Screen display command. |
| `view_voltage` | `0x55DD33BB` | View Voltage command. |
| `light` | `0x008844CC` | Crossed-out lightbulb command. |

## Optional Receiver

A receiver is not required for transmit-only control. If configured, the climate
entity decodes the full Countrymod packet, including the hidden 32-bit tail and
checksum. This allows handheld remote sync to resolve exact fan speeds above
Speed 3.

```yaml
remote_receiver:
  id: ir_rx
  pin:
    number: GPIO21
    inverted: true
    mode:
      input: true
      pullup: true
  dump: raw
  tolerance: 50%
  filter: 250us
  idle: 25ms

climate:
  - platform: countrymod
    id: rv_ac
    name: Climate
    transmitter_id: ir_tx
    receiver_id: ir_rx
    supports_heat: false
```

The `idle: 25ms` setting keeps each frame/tail pair in one receiver event while
still allowing the remote's two full-state packets to arrive as separate events.
The first packet updates shared state, and the second packet updates the exact
fan speed from the tail nibble.

`dump: raw` is useful while testing because the generic LG dump only shows the
first 32-bit frame. The Countrymod climate decoder uses the raw timing stream
internally; no raw data needs to be copied into YAML.

## Protocol Notes

### Timing

Countrymod climate packets use a 38 kHz carrier and NEC/LG-like pulse timings:

| Segment | Timing |
| --- | --- |
| Header mark | 9000 us |
| Header space | 4500 us |
| Bit mark | 560 us |
| Zero space | 520 us |
| One space | 1690 us |
| Frame-to-tail space | 20000 us |
| Default packet gap | 40000 us |
| Final frame gap | 25300 us |

A one-shot command is a single 32-bit frame plus a fixed `010` trailer. A climate
packet is a 32-bit frame plus the `010` trailer, followed by a 32-bit tail. A
full climate update sends two climate packets: first marker `0x58`, then second
marker `0x78`.

### State Layout

The component builds an 8-byte logical state, computes the checksum, reverses the
bits in each byte, then transmits the first four bytes as the LG-like frame and
the last four bytes as the tail.

| Byte | Field |
| --- | --- |
| `0` | Control byte: mode, power, fan display code, Negative Ion, Night. |
| `1` | Temperature code, normally Celsius minus 16. Eco uses fixed code `0x08`. |
| `2` | Flags: `0x10` Turbo, `0x80` reserved/undocumented. |
| `3` | Packet marker: `0x58` first packet, `0x78` second packet. |
| `4` | Reserved, currently `0x00`. |
| `5` | First packet uses `0x20`; second packet uses `0x00`. |
| `6` | Second packet exact fan speed in the high nibble. |
| `7` | Checksum in the high nibble; Eco second packet uses low nibble `0x03`. |

Control byte low bits:

| Bits | Meaning |
| --- | --- |
| `0..2` | Mode base: `0x00` Eco, `0x01` Cool, `0x03` Fan Only, `0x04` Heat. |
| `3` | Power/on bit when `use_power_bit` is enabled. |
| `4..5` | Display fan code: Auto `0`, Speed 1 `1`, Speed 2 `2`, Speed 3-5 `3`. |
| `6` | Negative Ion. |
| `7` | Night. |

Checksum starts at `0x0A`, adds the low nibbles of bytes 0-3 and the high
nibbles of bytes 5-7, then stores the low 4 bits of the sum in byte 7 high
nibble.

### Captured Fan Examples

Cool mode at 72 F / 22 C, power on, no Eco/Turbo/Night/Negative Ion:

| Fan setting | First packet | Second packet |
| --- | --- | --- |
| Auto | `9060001A/0004000C` | `9060001E/00000008` |
| Speed 1 | `9860001A/0004000C` | `9860001E/00000804` |
| Speed 2 | `9460001A/0004000C` | `9460001E/0000040C` |
| Speed 3 | `9C60001A/0004000C` | `9C60001E/00000C02` |
| Speed 4 | `9C60001A/0004000C` | `9C60001E/0000020A` |
| Speed 5 | `9C60001A/0004000C` | `9C60001E/00000A06` |

`Turbo` uses the protocol turbo flag and transmits with max-fan fields. `Eco`
uses the Eco mode base and the Eco tail nibble.

# Countrymod RV AC ESPHome Component

ESPHome external component for Countrymod RV air conditioners that use the
YAP0F/YAP0F3 handheld IR remote protocol.

This project is vibe-coded and provided as-is. Use it at your own risk.

This has been tested with the CountryMod 12V DC RV Air Conditioner 10000 BTU RV
AC Unit sold on Amazon under ASIN `B0DW3TYSNR`.

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

`rmt_symbols: 192` and `non_blocking: false` are recommended for reliable
transmission with this component.

Set `supports_heat: false` for cool-only units. Omit it, or set it to `true`, if
your unit responds to heat commands.

## Entities

| Platform | Type | Purpose |
| --- | --- | --- |
| `climate.countrymod` | `Climate` | Power, HVAC mode, target temperature, and fan speed. |
| `select.countrymod` | `Mode` | Remote mode selector: `Auto`, `Eco`, or `Turbo`. |
| `switch.countrymod` | `night` | Night/sleep flag. |
| `switch.countrymod` | `negative_ion` | Negative Ion flag from the remote/manual. |
| `button.countrymod` | `display` | Triggers the remote's `DISP.` function. |
| `button.countrymod` | `view_voltage` | Triggers the remote's View Voltage function. |
| `button.countrymod` | `light` | Triggers the crossed-out lightbulb function. |

The `Mode` select is the canonical control for the remote's `AUTO`, `ECO`, and
`TURBO` buttons. `Auto` means Eco and Turbo are both off; it is not a separate
climate auto HVAC mode. Eco and Turbo are mutually exclusive.

Fan control exposes standard `Auto` plus custom fan speeds `Speed 1` through
`Speed 5`, matching the handheld remote. Fan Only accepts target temperature
changes on this unit.

Home Assistant can show Fahrenheit while ESPHome stores temperatures in Celsius.
The target temperature range is 16-30 C, equivalent to roughly 61-86 F.

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
| `receiver_id` | optional | Remote receiver used to observe handheld remote activity. |
| `supports_cool` | `true` | Exposes cool mode. |
| `supports_heat` | `true` | Exposes heat mode. |
| `inter_frame_delay` | `40ms` | Delay used by the Countrymod climate transmission sequence. |
| `use_power_bit` | `true` | Enables normal on/off behavior for YAP0F/YAP0F3 remotes. |

Leave `inter_frame_delay: 40ms` unless you are testing a different remote
variant.

### Mode Select

```yaml
select:
  - platform: countrymod
    countrymod_id: rv_ac
    name: Mode
```

Options are `Auto`, `Eco`, and `Turbo`. `Auto` is the default state at startup
unless the handheld remote syncs Eco or Turbo.

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

| Type | Description |
| --- | --- |
| `display` | Remote `DISP.` function. |
| `view_voltage` | Remote View Voltage function. |
| `light` | Remote crossed-out lightbulb function. |

## Optional Receiver

A receiver is not required for transmit-only control. If configured, the climate
entity observes the handheld remote and keeps Home Assistant state in sync,
including the exact fan speed.

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

The receiver settings shown above are recommended for the YAP0F/YAP0F3 remote.
`dump: raw` is useful while troubleshooting receiver issues, but no captured raw
data needs to be copied into YAML.

## Protocol Notes

This protocol is compatible with the YAP0F/YAP0F3-style Gree/Kelvinator packet
family, but the field names below describe the behavior captured from the
CountryMod RV AC remote. A normal climate command is full-state and sends two
complete state packets, not a short button command. ESPHome's generic receiver
often reports the first 32 bits as `remote.lg`, but the complete Countrymod
message also includes trailer bits, hidden tail data, checksum, and captured
timing.

### Packet Shape

A normal climate update sends two full-state packets. Each packet is made from an
8-byte logical state:

```text
packet = header + frame32 + trailer010 + mark + frame_to_tail_space + tail32 + mark + packet_gap
frame32 = bit_reverse_each_byte(state[0..3]) packed MSB first
tail32  = bit_reverse_each_byte(state[4..7]) packed MSB first
```

Byte bit reversal means bit 0 becomes bit 7, bit 1 becomes bit 6, and so on.
For example, logical byte `0x09` (`00001001`) is transmitted as `0x90`
(`10010000`) inside the frame word.

The first packet uses marker byte `0x58`. The second packet uses marker byte
`0x78`. The second packet repeats the same main state and adds the exact fan
speed in the tail, which is required to distinguish Speed 3, Speed 4, and Speed
5.

One-shot button commands are shorter: they send only a 32-bit command frame plus
the fixed `010` trailer and final mark. They do not include an 8-byte climate
state or tail.

For receiving, `idle: 25ms` keeps each frame/tail pair in one receiver event
while still allowing the remote's two full-state packets to arrive as separate
events. The first packet updates shared state, and the second packet updates the
exact fan speed from the tail nibble.

### Timing

The carrier is 38 kHz. Timing is NEC/LG-like and tolerant, but these are the
captured values used by this component:

| Segment | Timing |
| --- | --- |
| Header mark | 9000 us |
| Header space | 4500 us |
| Bit mark | 560 us |
| Zero space | 520 us |
| One space | 1690 us |
| Frame-to-tail space | 20000 us |
| Gap after first climate packet | 40000 us by default |
| Gap after one-shot command or final climate packet | 25300 us |

A bit is encoded as one mark/space pair. A `1` bit is `560,-1690`; a `0` bit is
`560,-520`. Frame bits, trailer bits, and tail bits are sent most-significant bit
first after each logical byte has been bit-reversed.

### Logical State

| Byte | Field |
| --- | --- |
| `0` | Control byte: mode, power, fan display code, Negative Ion, Night. |
| `1` | Temperature code: Celsius minus 16. Range `0x00`-`0x0E` means 16-30 C. Fan Only also uses this value. Eco uses fixed code `0x08`. |
| `2` | Flags: `0x10` Turbo, `0x80` reserved/undocumented. |
| `3` | Packet marker: `0x58` first packet, `0x78` second packet. |
| `4` | Reserved, currently `0x00`. |
| `5` | Tail marker: first packet `0x20`, second packet `0x00`. |
| `6` | Second packet exact fan speed in the high nibble. First packet uses `0x00`. |
| `7` | Checksum in the high nibble. Eco second packet uses low nibble `0x03`; otherwise low nibble is `0x00`. |

Control byte bits:

| Bits | Meaning |
| --- | --- |
| `0..2` | Mode base: `0x00` Eco, `0x01` Cool, `0x03` Fan Only, `0x04` Heat. |
| `3` | Power/on bit when `use_power_bit` is enabled. Cleared for off packets. |
| `4..5` | Display fan code: Auto `0`, Speed 1 `1`, Speed 2 `2`, Speed 3-5 `3`. |
| `6` | Negative Ion. |
| `7` | Night. |

Fan speed encoding:

| Fan setting | Control byte fan code | Second packet byte 6 high nibble |
| --- | --- | --- |
| Auto | `0` | `0` |
| Speed 1 | `1` | `1` |
| Speed 2 | `2` | `2` |
| Speed 3 | `3` | `3` |
| Speed 4 | `3` | `4` |
| Speed 5 | `3` | `5` |

Turbo is a flag in byte 2 and transmits as Cool with Speed 5. Eco and Turbo are
mutually exclusive. Selecting Fan Only or Heat clears both Eco and Turbo.

### Checksum

Compute the checksum before bit-reversing bytes. Byte 7's high nibble must be
clear while computing the checksum.

```text
sum = 0x0A
sum += state[0] & 0x0F
sum += state[1] & 0x0F
sum += state[2] & 0x0F
sum += state[3] & 0x0F
sum += (state[5] >> 4) & 0x0F
sum += (state[6] >> 4) & 0x0F
sum += (state[7] >> 4) & 0x0F   # normally 0 before storing checksum
state[7] = ((sum & 0x0F) << 4) | (state[7] & 0x0F)
```

To decode a received packet, reverse the bits of each frame/tail byte back into
logical byte order, clear byte 7's high nibble, recompute the checksum, and
compare it with the received byte 7 high nibble.

### One-Shot Commands

| Button | Frame | Full frame bits before timing |
| --- | --- | --- |
| Display | `0x22AA66EE` | `0x22AA66EE` followed by trailer `010` |
| View Voltage | `0x55DD33BB` | `0x55DD33BB` followed by trailer `010` |
| Light | `0x008844CC` | `0x008844CC` followed by trailer `010` |

### Full Climate Examples

Each row shows the logical 8-byte state first, then the transmitted 32-bit
`frame/tail` words after per-byte bit reversal. A full command sends the first
packet, waits the packet gap, then sends the second packet.

Cool, 22 C / 72 F, fan Auto, power on:

| Packet | Logical state bytes | Transmitted frame/tail |
| --- | --- | --- |
| First | `09 06 00 58 00 20 00 30` | `9060001A/0004000C` |
| Second | `09 06 00 78 00 00 00 10` | `9060001E/00000008` |

The first packet frame bits are:

```text
10010000011000000000000000011010 010
```

The first packet tail bits are:

```text
00000000000001000000000000001100
```

Fan Only, 22 C / 72 F, Speed 3, power on:

| Packet | Logical state bytes | Transmitted frame/tail |
| --- | --- | --- |
| First | `3B 06 00 58 00 20 00 50` | `DC60001A/0004000A` |
| Second | `3B 06 00 78 00 00 30 60` | `DC60001E/00000C06` |

Cool, 16 C / 61 F, Turbo, Speed 5, power on:

| Packet | Logical state bytes | Transmitted frame/tail |
| --- | --- | --- |
| First | `39 00 10 58 00 20 00 D0` | `9C00081A/0004000B` |
| Second | `39 00 10 78 00 00 50 00` | `9C00081E/00000A00` |

Cool, 22 C / 72 F, Speed 1, power off:

| Packet | Logical state bytes | Transmitted frame/tail |
| --- | --- | --- |
| First | `11 06 00 58 00 20 00 B0` | `8860001A/0004000D` |
| Second | `11 06 00 78 00 00 10 A0` | `8860001E/00000805` |

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

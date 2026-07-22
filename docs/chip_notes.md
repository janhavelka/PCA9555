# PCA9555 Chip Notes

This page preserves implementation-relevant chip facts from the original
datasheet extraction. It is not a replacement for the TI datasheet; use it as a
checklist when reviewing driver behavior, board bring-up, and field integration.

Primary sources:

- TI PCA9555 datasheet, SCPS131J, revised March 2021
- TI application note SLVAFL0, I2C auto-increment feature
- Local implementation extraction: `datasheet_extraction.md`

## Identity And Limits

- PCA9555 is a 16-bit I2C/SMBus I/O expander with two 8-bit ports:
  `P00-P07` and `P10-P17`.
- Supply range is 2.3 V to 5.5 V.
- I2C supports Standard Mode up to 100 kHz and Fast Mode up to 400 kHz.
- The 7-bit address range is `0x20` through `0x27`, selected by A0, A1, and A2.
- A0/A1/A2 must be tied high or low and must not float. Do not change address
  pins between an I2C START and STOP.
- PCA9555 has no chip-ID register. An ACK or configuration-register read proves
  address response only, not device identity.
- PCA9555 shares address space with other I2C I/O expanders. PCA9535-style
  substitutions must account for the PCA9555 internal pull-ups.
- The device does not respond to the I2C general-call address.
- There is no software reset command and no reset pin. Reset requires power
  cycling VCC through the POR thresholds.

## I/O Electrical Notes

- P-port and address inputs are 5 V tolerant up to 5.5 V across the operating
  VCC range. SCL and SDA high-level input must not exceed VCC.
- Each I/O pin has an internal approximately 100 kOhm pull-up when used as an
  input. The pull-up is not software configurable.
- Inputs held low draw additional standby current through the internal pull-up.
  For low-power designs, leave unused inputs high or configure them as outputs
  driven high when safe.
- Active-low LED loads from VCC can bias an off input pin near `VCC - Vf` and
  increase standby current. Use a documented mitigation such as a parallel
  resistor or separate LED supply rail when that circuit style is required.
- Output pins are push-pull. Per-pin operating limits are 25 mA sink and
  10 mA source.
- Current must also be limited at port and device level: 100 mA sink per 8-bit
  port, 80 mA source per 8-bit port, 200 mA total sink, and 160 mA total source.
- Reading an Output Port register returns the output latch flip-flop, not the
  physical pin voltage. Read the Input Port registers for sampled pin level.
- Input Port registers reflect pin level even when the pin is configured as an
  output.
- Do not externally drive a pin that is configured as an output.

## Register And Auto-Increment Notes

- The eight command registers are four port pairs: Input, Output, Polarity
  Inversion, and Configuration.
- Power-on defaults are output latches `0xFFFF`, polarity `0x0000`, and
  configuration `0xFFFF` (all pins input). Input registers are pin-dependent.
- Auto-increment is pair-local. A 2-byte access from an even register reads or
  writes the matching Port 0/Port 1 pair. Access does not advance into the next
  pair.
- Writing the desired output latch before clearing configuration bits avoids
  output-enable glitches. Prefer APIs that make the preload explicit when
  changing pins from input to output.
- Command byte bits above the documented `0x00` through `0x07` range are not
  specified by the datasheet. Keep command bytes in the documented range.

## Interrupt Notes

- INT is open-drain, active low, and requires an external pull-up.
- A pin configured as an output does not generate input-change interrupts.
- Reading an input port clears interrupt state for that port. To clear pending
  sources from both ports, read both Input Port registers.
- The interrupt clear point is associated with the read ACK/NACK phase. Input
  transitions during that clock can be lost or create very short INT pulses, so
  debounce or re-read in application policy when edge certainty matters.
- Changing a pin from output to input can cause a false interrupt if the sampled
  pin level differs from the previous input-register state.
- Interrupt output timing is microsecond-scale; debounce and event policy belong
  in the application.

## Interrupt Errata

The datasheet documents an interrupt errata condition:

- If the PCA9555 command pointer is left at Input Port 0 (`0x00`) and another
  slave on the same bus acknowledges a read address, PCA9555 INT can be
  improperly de-asserted.
- The workaround is to write a command byte other than `0x00` after reading
  input ports before allowing another target read on the shared bus.
- The library parks the pointer at Output Port 0 (`0x02`) when the workaround is
  enabled.
- On a shared bus, serialize the input read and pointer-park write so no other
  target transaction interleaves between them.

## Power And Layout Notes

- Ensure VCC is stable above the POR rising threshold before first I2C access.
  No exact post-threshold startup delay is specified in the datasheet.
- For a full reset, VCC must fall below the POR falling threshold before rising
  again.
- Add external pull-ups for SDA, SCL, and INT. Size SDA/SCL pull-ups for bus
  capacitance and selected speed.
- Place a 0.1 uF bypass capacitor close to VCC/GND. A larger bulk capacitor can
  help with short supply disturbances.
- Keep power and ground routing low impedance. The datasheet layout guidance
  does not require high-speed differential-style constraints for normal I2C
  speeds.

## Hardware Checklist

- Strap A0/A1/A2 deliberately and document the resulting address.
- Use current-limited loads when validating outputs.
- Verify output latch defaults, polarity defaults, and configuration defaults
  after a true power cycle.
- Confirm all input levels with known released/high and pulled-low states.
- Capture INT assertion/clear behavior with the final pull-up value.
- Confirm application re-read or debounce policy for transitions near the input
  read ACK/NACK edge.
- For shared buses, capture or otherwise prove the errata pointer park happens
  before any other target read.

# PCA9555 Register Reference

This reference summarizes the eight PCA9555 command-byte registers and the
library rules that apply to direct access helpers.

## Register Map

| Reg | Name | Access | Reset value | Meaning |
| --- | --- | --- | --- | --- |
| `0x00` | Input Port 0 | R | Pin-dependent | Input-register sense for P00-P07. Values reflect physical logic levels after configured polarity inversion; they are not fixed reset constants. |
| `0x01` | Input Port 1 | R | Pin-dependent | Input-register sense for P10-P17. Values reflect physical logic levels after configured polarity inversion; they are not fixed reset constants. |
| `0x02` | Output Port 0 | R/W | `0xFF` | Latched output state for P00-P07. Pins follow these bits only when configured as outputs. |
| `0x03` | Output Port 1 | R/W | `0xFF` | Latched output state for P10-P17. Pins follow these bits only when configured as outputs. |
| `0x04` | Polarity Inversion 0 | R/W | `0x00` | `1 = invert input sense`, `0 = normal input sense` for P00-P07. |
| `0x05` | Polarity Inversion 1 | R/W | `0x00` | `1 = invert input sense`, `0 = normal input sense` for P10-P17. |
| `0x06` | Configuration 0 | R/W | `0xFF` | `1 = input/high-Z`, `0 = push-pull output` for P00-P07. POR makes all pins inputs. |
| `0x07` | Configuration 1 | R/W | `0xFF` | `1 = input/high-Z`, `0 = push-pull output` for P10-P17. POR makes all pins inputs. |

Combined defaults after a true PCA9555 power-on reset:

- Output latches: `0xFFFF`
- Polarity inversion: `0x0000`
- Configuration: `0xFFFF`, all pins input
- Input registers: pin-dependent; verify against physical levels, not a fixed
  byte pattern

## Pair Auto-Increment

The PCA9555 auto-increments within the selected register pair only:

| Pair | Forward order | Odd-start wrap |
| --- | --- | --- |
| Input | `0x00` -> `0x01` | `0x01` -> `0x00` |
| Output | `0x02` -> `0x03` | `0x03` -> `0x02` |
| Polarity | `0x04` -> `0x05` | `0x05` -> `0x04` |
| Configuration | `0x06` -> `0x07` | `0x07` -> `0x06` |

Access never crosses from one pair to the next in a single PCA9555 transaction.
The library direct bulk helpers are therefore limited to one or two bytes within
one pair.

## Direct Access Rules

- `readRegister()` reads a single register.
- `writeRegister()` writes a single writable register (`0x02` through `0x07`).
- `readRegisters()` and `writeRegisters()` are pair-bounded bulk helpers.
- Input registers are read-only; writes to `0x00` or `0x01` are rejected by the
  public direct write APIs.
- Successful direct access synchronizes the corresponding cached runtime state.
- Failed direct writes mark `hardwareStateDirty()` because hardware may have
  accepted one data byte while the driver cache remained unchanged.

## Presence, Defaults, and Recovery

- `probe()` means the configured address responded to a raw configuration-register
  read. It is not chip-ID proof because PCA9555 has no identity register.
- `begin()` default checks are plausibility checks against Configuration Port
  POR defaults (`0xFF/0xFF`), not identity proof.
- `recover()` re-probes and reapplies cached output, polarity, and configuration
  state. It cannot force a true PCA9555 power-on reset.

## Interrupt and Errata Notes

- INT is active-low/open-drain and requires a pull-up.
- Reading an input port clears interrupt state for that port only. Reading Port 0
  does not clear Port 1, and reading Port 1 does not clear Port 0.
- `readInputs()`, `readInputsAndClearInterrupt()`, and `clearInterrupts()` read
  both input ports and therefore clear both port sources.
- Pins configured as outputs do not generate input-change interrupts.
- Changing an output to an input can cause false interrupt behavior if the
  sampled pin state differs from the previous input-register state.
- The PCA9555 interrupt errata workaround parks the command pointer at
  `cmd::ERRATA_SAFE_CMD` (`0x02`) after input reads. On a shared bus, the input
  read and pointer-park write must be serialized so no other target transaction
  interleaves between them.

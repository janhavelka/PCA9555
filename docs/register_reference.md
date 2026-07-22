# PCA9555 Register Reference

This reference summarizes the eight PCA9555 command-byte registers and the
library rules that apply to direct access helpers.

Primary source:

- [TI PCA9555 datasheet](https://www.ti.com/lit/ds/symlink/pca9555.pdf)

## Register Map

| Reg | Name | Access | Reset value | Meaning |
| --- | --- | --- | --- | --- |
| `0x00` | Input Port 0 | R | Pin-dependent | Input-register sense for P00-P07. Values reflect physical logic levels after configured polarity inversion; they are not fixed reset constants. |
| `0x01` | Input Port 1 | R | Pin-dependent | Input-register sense for P10-P17. Values reflect physical logic levels after configured polarity inversion; they are not fixed reset constants. |
| `0x02` | Output Port 0 | R/W | `0xFF` | Latched output state for P00-P07. Pins follow these bits only when configured as outputs. |
| `0x03` | Output Port 1 | R/W | `0xFF` | Latched output state for P10-P17. Pins follow these bits only when configured as outputs. |
| `0x04` | Polarity Inversion 0 | R/W | `0x00` | `1 = invert input sense`, `0 = normal input sense` for P00-P07. |
| `0x05` | Polarity Inversion 1 | R/W | `0x00` | `1 = invert input sense`, `0 = normal input sense` for P10-P17. |
| `0x06` | Configuration 0 | R/W | `0xFF` | `1 = input with output driver high-Z and internal pull-up present`, `0 = push-pull output` for P00-P07. POR makes all pins inputs. |
| `0x07` | Configuration 1 | R/W | `0xFF` | `1 = input with output driver high-Z and internal pull-up present`, `0 = push-pull output` for P10-P17. POR makes all pins inputs. |

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
- `writeRegister()` writes one Output or Polarity register (`0x02` through
  `0x05`). Direct Configuration writes (`0x06`/`0x07`) are rejected.
- `readRegisters()` and `writeRegisters()` are pair-bounded bulk helpers.
- Input registers are read-only; writes to `0x00` or `0x01` are rejected by the
  public direct write APIs.
- Direct reads update `ObservedState`; they do not silently replace caller
  intent or establish the protocol shadow used by cached writes. A contradictory
  read invalidates that shadow pair so a later cached RMW cannot clobber bits.
- Explicit `startVerifyImage()` may re-establish matching pairs because its
  complete comparison image is supplied by the caller; mismatched pairs remain
  fenced.
- Raw Configuration-register writes are rejected because they can bypass the
  required output-latch preload before enabling an output.
- Other direct writes invalidate the whole affected shadow pair before the
  physical attempt. Only a complete, successful two-register pair write can
  re-establish that shadow pair. A failed write can leave the pair uncertain
  when the transport cannot prove that no register data was accepted.
- Cached read-modify-write helpers fail with `SHADOW_INVALID` or
  `STATE_UNCERTAIN` when their required pair is not safe to use.

## Presence, Defaults, and Reconciliation

- `bind()` and its `begin()` compatibility alias perform no I2C.
- `probe()` means the configured address responded to one raw
  Configuration-register read. It is not chip-ID proof because PCA9555 has no
  identity register. Probe is diagnostic and does not update health counters.
- `checkPorDefaults()` explicitly reads Configuration Port 0/1 and compares
  them with `0xFF/0xFF`. This is plausibility evidence, not identity proof.
- Recovery policy belongs to the caller. `startApplyImage()` applies and reads
  back a complete caller-owned image. `startVerifyImage()` checks an expected
  image without selecting policy. Neither can force a true PCA9555 power-on
  reset.

## Interrupt and Errata Notes

- INT is active-low/open-drain and requires a pull-up.
- Reading an input port clears interrupt state for that port only. Reading Port 0
  does not clear Port 1, and reading Port 1 does not clear Port 0.
- The clear point is tied to the read ACK/NACK phase; an input transition during
  that clock can be lost or produce a very short INT pulse. Re-read or debounce
  at the application level when edge certainty matters.
- `readInputs()`, `readInputsAndClearInterrupt()`, and `clearInterrupts()` read
  both input ports and therefore clear both port sources.
- Pins configured as outputs do not generate input-change interrupts.
- Changing an output to an input can cause false interrupt behavior if the
  sampled pin state differs from the previous input-register state.
- The PCA9555 interrupt errata workaround parks the command pointer at
  `cmd::ERRATA_SAFE_CMD` (`0x02`) after input reads. On a shared bus, the input
  read and pointer-park write are one owner-exclusive protocol sequence; no
  other operation on this driver may interleave between them. Cooperative
  cancellation or whole-operation timeout does not silently discard required
  cleanup after the input read has completed.

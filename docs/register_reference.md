# PCA9555 Register Reference

This reference summarizes the eight PCA9555 registers and the library rules that apply to direct access helpers.

## Register Map

| Reg | Name | Access | Default | Notes |
| --- | --- | --- | --- | --- |
| `0x00` | Input Port 0 | R | `0xFF` | Physical pin level for P00-P07 |
| `0x01` | Input Port 1 | R | `0xFF` | Physical pin level for P10-P17 |
| `0x02` | Output Port 0 | R/W | `0xFF` | Latched output state for P00-P07 |
| `0x03` | Output Port 1 | R/W | `0xFF` | Latched output state for P10-P17 |
| `0x04` | Polarity Inversion 0 | R/W | `0x00` | `1 = invert input sense` |
| `0x05` | Polarity Inversion 1 | R/W | `0x00` | `1 = invert input sense` |
| `0x06` | Configuration 0 | R/W | `0xFF` | `1 = input`, `0 = output` |
| `0x07` | Configuration 1 | R/W | `0xFF` | `1 = input`, `0 = output` |

## Direct Access Rules

- `readRegister()` reads a single register.
- `writeRegister()` writes a single writable register.
- `readRegisters()` and `writeRegisters()` are pair-bounded bulk helpers.
- Bulk access is limited to 1 or 2 bytes and must not cross a register-pair boundary.
- Input registers are read-only and are only valid for read helpers.
- The driver keeps its cached runtime state synchronized after successful direct access.

## Pair Boundaries

The PCA9555 auto-increments within each register pair only:

- `0x00` -> `0x01`
- `0x02` -> `0x03`
- `0x04` -> `0x05`
- `0x06` -> `0x07`

Access never crosses from one pair to the next in a single transaction.

## Driver Notes

- `begin()` verifies device presence and applies the current runtime `Config`.
- `probe()` uses raw transport and does not update health.
- `recover()` re-probes and reapplies the active settings snapshot.
- `Config::applyInterruptErrata` keeps the pointer away from `0x00` after input reads.

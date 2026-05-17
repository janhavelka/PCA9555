# Register Map

| Command | Register | Access | Power-up default | Notes | Source |
|---:|---|---|---:|---|---|
| `0x00` | Input Port 0 | Read | pin-dependent | Reflects P00-P07 logic levels. Writes have no effect. | datasheet, pp. 19-20 |
| `0x01` | Input Port 1 | Read | pin-dependent | Reflects P10-P17 logic levels. Writes have no effect. | datasheet, pp. 19-20 |
| `0x02` | Output Port 0 | Read/write | `0xFF` | Output latch for P00-P07 when configured as outputs. | datasheet, pp. 19-20 |
| `0x03` | Output Port 1 | Read/write | `0xFF` | Output latch for P10-P17 when configured as outputs. | datasheet, pp. 19-20 |
| `0x04` | Polarity Inversion Port 0 | Read/write | `0x00` | `1` inverts corresponding input polarity. | datasheet, pp. 19-20 |
| `0x05` | Polarity Inversion Port 1 | Read/write | `0x00` | `1` inverts corresponding input polarity. | datasheet, pp. 19-20 |
| `0x06` | Configuration Port 0 | Read/write | `0xFF` | `1` = input/high impedance, `0` = output. | datasheet, pp. 19-20 |
| `0x07` | Configuration Port 1 | Read/write | `0xFF` | `1` = input/high impedance, `0` = output. | datasheet, pp. 19-20 |

Output-register reads return the latch value, not necessarily the physical pin level. Input-register reads report the input-register sense; with normal polarity this is the physical pin state regardless of whether the pin is configured as input or output. Source: datasheet, p. 20.

Command-byte format is `00000 B2 B1 B0`; only the low three bits select one of the eight registers. Source: datasheet, p. 19.

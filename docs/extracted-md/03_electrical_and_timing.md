# Electrical And Timing

Electrical limits below are PCA9555-specific extracted values; full board current and thermal design still needs the complete datasheet tables.

| Parameter | Value | Source |
|---|---:|---|
| VCC operating range | 2.3 V to 5.5 V | datasheet, pp. 1, 7 |
| Standby current | 1 uA max feature headline | datasheet, p. 1 |
| I2C clock | 0 kHz to 400 kHz | datasheet, pp. 7-8 |
| I2C bus capacitive load | 400 pF max | datasheet, pp. 7-8 |
| I2C output fall time | 300 ns max for 10 pF to 400 pF bus | datasheet, p. 7 |
| Interrupt valid time | 4 us | datasheet, p. 8 |
| Interrupt reset delay | 4 us | datasheet, p. 8 |
| Output data valid from SCL to P port | 200 ns max | datasheet, p. 8 |
| P-port input setup to SCL | 150 ns min | datasheet, p. 8 |
| P-port input hold from SCL | 1 us min | datasheet, p. 8 |

Power-on reset initializes registers and the I2C/SMBus state machine when VCC reaches the POR threshold. Table 10-1 gives `VCC_FT` 1 ms to 100 ms, `VCC_RT` 0.01 ms to 100 ms, `VCC_TRR_GND` 0.001 ms min, `VCC_TRR_POR50` 0.001 ms min, `VPORF` 0.767 V to 1.144 V, and `VPORR` 1.033 V to 1.428 V. Source: datasheet, p. 27.

Timing notes:

- Standard-mode limits are `fscl <= 100 kHz`, SCL high >= 4 us, SCL low >= 4.7 us, SDA setup >= 250 ns, bus-free >= 4.7 us. Source: datasheet, pp. 7-8.
- Fast-mode limits are `fscl <= 400 kHz`, SCL high >= 0.6 us, SCL low >= 1.3 us, SDA setup >= 100 ns, bus-free >= 1.3 us. Source: datasheet, p. 8.
- INT timing is asynchronous to application code but bounded by `tiv` and `tir`, both 4 us max in the switching table. Source: datasheet, p. 8.
- Output-port latch defaults to `0xFF`; write desired latch values before changing Configuration bits from `1` input to `0` output if a low initial output is required. Source: datasheet, pp. 19-20.

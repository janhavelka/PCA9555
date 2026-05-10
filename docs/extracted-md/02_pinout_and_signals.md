# Pinout And Signals

Pin numbers vary by package, but the logical signals are stable across package options. Source: datasheet, pp. 4-5.

| Signal | Direction | Notes | Source |
|---|---|---|---|
| `VCC` | Power | 2.3 V to 5.5 V operating supply. | datasheet, pp. 1, 7 |
| `GND` | Power | Ground reference. | datasheet, p. 5 |
| `SDA` | I2C data | Open-drain bidirectional bus line; use normal I2C pull-ups. | datasheet, pp. 5, 18 |
| `SCL` | I2C clock | I2C clock input. | datasheet, pp. 5, 18 |
| `A0`, `A1`, `A2` | Inputs | Tie each directly to VCC or ground; they select addresses `0x20-0x27`. Do not change during transactions. | datasheet, pp. 5, 19 |
| `INT` | Open-drain output | Active-low interrupt; connect to VCC through a pull-up resistor. | datasheet, pp. 5, 15-16 |
| `P00-P07` | GPIO port 0 | Input by default after POR. | datasheet, pp. 1, 19-20 |
| `P10-P17` | GPIO port 1 | Input by default after POR. | datasheet, pp. 1, 19-20 |

The I/O pins are 5-V tolerant and can directly drive LEDs within datasheet current limits. Source: datasheet, p. 1.

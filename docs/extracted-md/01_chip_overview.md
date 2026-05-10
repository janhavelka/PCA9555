# Chip Overview

The PCA9555 is a 16-bit I2C/SMBus general-purpose I/O expander with two 8-bit ports. It provides Input Port, Output Port, Polarity Inversion, and Configuration registers for each port. At power-on, all I/Os are configured as inputs. Source: datasheet, p. 1.

Key documented facts:

| Feature | Fact | Source |
|---|---|---|
| I/O width | 16 GPIO pins as P00-P07 and P10-P17 | datasheet, pp. 1, 4 |
| Supply | 2.3 V to 5.5 V VCC operation | datasheet, p. 1 |
| I2C speed | Fast-mode compatible up to 400 kHz | datasheet, pp. 1, 7-8 |
| Addressing | Three hardware pins allow eight addresses, `0x20-0x27` | datasheet, p. 19 |
| Interrupt | Open-drain active-low INT reports input changes | datasheet, pp. 1, 15-16 |
| Reset state | Configuration registers default to `0xFF`; outputs default high in output latch; polarity defaults normal | datasheet, pp. 19-20 |

The PCA9555 is register-based, not memory-based. All software-visible state is accessed through the eight command-byte registers `0x00` through `0x07`; interrupt state is not a register bit and is cleared by input-port read behavior. Source: datasheet, pp. 15-16, 19-23.

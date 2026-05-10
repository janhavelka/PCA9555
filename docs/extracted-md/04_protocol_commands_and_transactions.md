# Protocol Commands And Transactions

The 7-bit I2C address is `0100 A2 A1 A0`, which gives `0x20-0x27`. The R/W bit follows the 7-bit address in the I2C address byte. Source: datasheet, pp. 18-19.

| Transaction | Sequence | Source |
|---|---|---|
| Write register | START, address+W, command byte, data byte(s), STOP | datasheet, p. 21 |
| Read register | START, address+W, command byte, repeated START, address+R, read byte(s), NACK final byte, STOP | datasheet, pp. 21-23 |
| Current command read | After a command byte has been set, reads continue from that addressed register until a new command byte is written. | datasheet, p. 19 |
| Multi-byte write/read | Registers operate as pairs. After one byte in a pair, the next byte accesses the other register in that pair. | datasheet, pp. 21-23 |

Pair auto-increment behavior is important. The eight registers are arranged as four pairs: input, output, polarity inversion, and configuration. Multi-byte accesses alternate within the selected pair rather than walking linearly through all eight registers. Source: datasheet, pp. 21-23; auto-increment app note, pp. 4-5.

Transaction facts:

- A 16-bit input read uses command `0x00` followed by two read bytes: Input Port 0 then Input Port 1. Source: datasheet, pp. 21-23.
- A 16-bit output write uses command `0x02` followed by Output Port 0 then Output Port 1 bytes. Source: datasheet, p. 21.
- Pair wrap is documented: if the first byte is sent to register `0x03`, the next byte is stored in register `0x02`; if Input Port 1 is read first, the next byte read is Input Port 0. Source: datasheet, pp. 21-22.
- The PCA9555 datasheet does not document linear burst progression from `0x00` through `0x07`; it documents four alternating register pairs. Source: datasheet, pp. 21-23.

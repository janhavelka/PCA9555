# Modes, Interrupts, Status, And Faults

The PCA9555 has no status register. Interrupt state is represented by the open-drain INT pin and by reading input registers.

Interrupt behavior:

| Behavior | Source |
|---|---|
| INT asserts low when an input state differs from the corresponding Input Port register state. | datasheet, pp. 14-16 |
| INT is reset when input data is read or when the port changes back to the previously read state. | datasheet, pp. 15-16 |
| INT requires a pull-up resistor to VCC. | datasheet, pp. 5, 16 |
| Each 8-bit port is read independently, so interrupt clearing can interact with which input port was read. | datasheet, p. 16 |

Datasheet errata note: INT can be improperly deasserted if the last command byte written was `0x00` and another I2C slave acknowledges a read address on the bus. The errata condition depends on shared-bus traffic and the PCA9555 command pointer, not on any readable PCA9555 status bit. Source: datasheet, p. 16.

POR/fault notes:

- POR returns registers to defaults and initializes the I2C/SMBus state machine. Source: datasheet, pp. 14, 27.
- Invalid addresses or bus errors are transport-level failures; there are no device fault bits to read.
- Configuration defaults all pins to inputs, which is the safest post-reset state for shared or externally driven lines. Source: datasheet, pp. 1, 19-20.

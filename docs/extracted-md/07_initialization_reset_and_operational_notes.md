# Initialization, Reset, And Operational Notes

Recommended initialization flow:

1. Build the 7-bit address from base `0x20` plus A2/A1/A0 board straps.
2. Optionally read Input Port 0 and 1 to clear any pending INT condition and capture initial states.
3. Write Output Port 0/1 latch values before enabling output direction bits.
4. Write Polarity Inversion registers if the application wants logical inversion for inputs.
5. Write Configuration Port 0/1, using `1` for input and `0` for output.

Sources: datasheet, pp. 19-21.

Operational notes:

- Treat 16-bit operations as two 8-bit register-pair transfers or one two-byte pair transfer.
- Output Port register reads return the output latch flip-flop, not the physical pin value; bit-level updates need the latch value as their read/modify/write base. Source: datasheet, p. 20.
- Input Port reads can clear INT state; this read side effect is part of the PCA9555 interrupt model. Source: datasheet, pp. 15-16.
- Keep core code transport-agnostic and use injected I2C callbacks.

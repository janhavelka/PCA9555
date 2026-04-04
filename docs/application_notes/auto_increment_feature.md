# I2C Auto Increment Feature
**Source:** auto_increment_feature.pdf | **Doc #:** SLVAFL0 | **Pages:** 7

## Key Takeaways
- Auto increment automatically advances the register pointer on each consecutive byte read/written within a single I2C transaction (no repeated START or STOP needed)
- Not all I2C devices support auto increment — it is not part of the original I2C standard; check each device's datasheet
- IO expanders (like PCA9555/TCAL6416) use **paired register auto increment**: the pointer alternates between a port-0 and port-1 register pair instead of incrementing linearly through all registers
- Some devices (e.g., TCA8418 keypad scanner) require an explicit enable bit; others auto-increment by default
- Burst read/write via auto increment can cut I2C bus overhead roughly in half compared to individual register transactions

## Summary

This TI application note explains the I2C auto increment feature, where the internal register pointer of an I2C target device advances automatically after each byte is read or written, enabling multi-byte burst transfers within a single transaction. Without auto increment, each register access requires a full START → address → register → data → STOP sequence. With auto increment, only the first register address is sent, and subsequent bytes target successive registers.

The note distinguishes two types of auto increment behavior. **Linear auto increment** (e.g., TCA8418 keypad scanner with 46 registers) walks sequentially through the entire register map. **Paired/port-scoped auto increment** (e.g., 16-bit IO expanders like TCAL6416 and PCA9555) alternates between a register pair — for instance, writing to Output Port 0 (0x02) then Output Port 1 (0x03), then back to 0x02, and so on. This paired behavior means a single burst cannot initialize all registers linearly, but it offers a latency advantage: output pins update at every ACK within the burst, enabling rapid toggling without re-addressing.

The enable mechanism varies by device: many IO expanders auto-increment by default, while other devices require setting a configuration bit.

## Technical Details

### Standard I2C Transaction (no auto increment)

**Single write:** `S → [Addr+W] → ACK → [Register] → ACK → [Data] → ACK → P`

**Single read:** `S → [Addr+W] → ACK → [Register] → ACK → Sr → [Addr+R] → ACK → [Data] → NACK → P`

Each additional register requires a complete new transaction.

### Auto Increment Burst Write

```
S → [Addr+W] → ACK → [Register 0] → ACK → [Data₀] → ACK → [Data₁] → ACK → [Data₂] → ACK → [Data₃] → ACK → P
```

4 data bytes written: 6 total bytes on bus. Without auto increment: 4 × 3 = 12 bytes — **50% reduction**.

### Auto Increment Burst Read

```
S → [Addr+W] → ACK → [Register 5] → ACK → Sr → [Addr+R] → ACK → [Data₅] → ACK → [Data₆] → ACK → [Data₇] → ACK → [Data₈] → ACK → [Data₉] → NACK → P
```

The controller can continue reading as long as it ACKs; NACK + STOP terminates the burst.

### IO Expander Paired Register Behavior (PCA9555 / TCAL6416)

For a 16-bit IO expander with port 0 and port 1:

| Byte # | Target Register | Description |
|--------|----------------|-------------|
| 1 | 0x02 (Output Port 0) | First data byte |
| 2 | 0x03 (Output Port 1) | Second data byte |
| 3 | 0x02 (Output Port 0) | Wraps back to port 0 |
| 4 | 0x03 (Output Port 1) | Wraps back to port 1 |
| ... | alternating | continues |

This means:
- A single burst **cannot** walk through all register types (Input → Output → Polarity → Config)
- Separate transactions are needed for each register-pair group during initialization
- **Advantage:** Continuous fast output toggling — each ACK immediately updates the port, minimizing latency
- **Advantage for reads:** Polling both ports of an input register pair without re-sending the register address

### For Single-Port IO Expanders (e.g., TCA9536, 4-bit)

The auto increment loops the **same register** — every data byte writes to the same output register. This enables the fastest possible output updates (one byte of overhead, then continuous data at each ACK).

### Enabling Auto Increment

| Device | Behavior |
|--------|----------|
| TI IO Expanders (PCA9555, TCAL6416, TCA9536) | Auto increment is always on by default |
| TCA8418/TCA8418E Keypad Scanner | Must set bit 7 of register 0x01 (AI_EN) |
| Other devices | Check datasheet |

## Relevance to PCA9555 Implementation

The PCA9555 is a 16-bit IO expander with two 8-bit ports, and it uses the **paired register auto increment** pattern. When the driver writes to register 0x02 (Output Port 0), the next byte automatically goes to 0x03 (Output Port 1), then wraps back to 0x02. This means:

1. **Initialization** requires separate I2C transactions for each register-pair group (Output pair 0x02–0x03, Polarity pair 0x04–0x05, Configuration pair 0x06–0x07).
2. **Runtime port writes** can efficiently update both ports in a single 2-byte burst write starting at 0x02.
3. **Runtime port reads** can read both input ports (0x00–0x01) in a single 2-byte burst read.
4. The driver should use burst reads/writes for paired registers to reduce I2C bus traffic and improve throughput, especially when polling inputs or updating outputs at high frequency via the TCA9548A I2C switch.

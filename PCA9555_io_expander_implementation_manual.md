# PCA9555 — Comprehensive Implementation Extraction

---

## 1. Source Documents

| # | Filename | Description | Pages | TI Document # |
|---|----------|-------------|-------|---------------|
| 1 | `datasheet_PCA9555.pdf` | PCA9555 Remote 16-bit I2C and SMBus I/O Expander with Interrupt Output and Configuration Registers | 49 | SCPS131J (August 2005, revised March 2021) |
| 2 | `auto_increment_feature.pdf` | I2C: What is the Auto Increment Feature? (Application Note) | 7 | SLVAFL0 (July 2024) |

---

## 2. Device Identity and Variants

- **Part number**: PCA9555
  (datasheet_PCA9555.pdf, p.1)
- **Full title**: "Remote 16-bit I2C and SMBus I/O Expander with Interrupt Output and Configuration Registers"
  (datasheet_PCA9555.pdf, p.1)
- **Manufacturer**: Texas Instruments
- **Relation to other parts**:
  - PCA9555 is **identical to PCA9535**, except PCA9555 includes an internal I/O pullup resistor (~100 kΩ, see schematic) which pulls I/O to a default high when configured as input and undriven.
    (datasheet_PCA9555.pdf, p.14)
  - Pin-to-pin and I2C-address compatible with **PCF8575**, but software changes required due to enhancements.
    (datasheet_PCA9555.pdf, p.14)
  - Fixed I2C address space is shared with PCF8575, PCF8575C, and PCF8574; up to 8 of these in any combination may share the same bus.
    (datasheet_PCA9555.pdf, p.14)
- **No other variants** (PCA9555A, PCA9555C, PCA9555D, etc.) are documented in the reviewed materials. Only the base PCA9555 is described.

### Packages

| Package | Pins | Body Size (nom) | Part Marking |
|---------|------|------------------|--------------|
| SSOP (DB) | 24 | 8.20 mm × 5.30 mm | PD9555 |
| SSOP (DBQ) | 24 | 8.65 mm × 3.90 mm | PCA9555 (obsolete) |
| TVSOP (DGV) | 24 | 5.00 mm × 4.40 mm | PD9555 |
| SOIC (DW) | 24 | 15.4 mm × 7.50 mm | PCA9555 |
| TSSOP (PW) | 24 | 7.80 mm × 4.40 mm | PD9555 |
| VQFN (RGE) | 24 | 4.00 mm × 4.00 mm | PD9555 |

(datasheet_PCA9555.pdf, p.1, p.31)

### Addendum: Latch-Up and ESD Feature Ratings

- **Latch-up performance** exceeds 100 mA per JESD 78, Class II.
  (datasheet_PCA9555.pdf, p.1)
- **ESD protection** exceeds JESD 22:
  - 2000 V Human-Body Model (HBM), per ANSI/ESDA/JEDEC JS-001, all pins
  - 1000 V Charged-Device Model (CDM), per JEDEC specification JESD22-C101 or ANSI/ESDA/JEDEC JS-002, all pins
  (datasheet_PCA9555.pdf, p.1, p.5)
- **Note**: JEDEC document JEP155 states that 500-V HBM allows safe manufacturing with a standard ESD control process. JEDEC document JEP157 states that 250-V CDM allows safe manufacturing with a standard ESD control process.
  (datasheet_PCA9555.pdf, p.5)
- **Note**: The Features list on p.1 also references 200-V Machine Model (A115-A), but this is **not** included in the formal ESD Ratings table on p.5 (which only lists HBM and CDM).
  (datasheet_PCA9555.pdf, p.1 vs p.5)

---

## 3. High-Level Functional Summary

The PCA9555 is a **16-bit I/O expander** for the I2C / SMBus bus, operating from **2.3 V to 5.5 V VCC**. It provides 16 general-purpose I/O pins organized as two 8-bit ports (Port 0: P00–P07, Port 1: P10–P17). Each pin can be independently configured as input or output. Key capabilities:

- **16 I/O pins**, push-pull output structure, 5 V tolerant inputs
- **I2C interface** up to 400 kHz (Fast Mode)
- **3 hardware address pins** (A0, A1, A2) → up to 8 devices on one bus
- **8 internal registers**: Input Port 0/1, Output Port 0/1, Polarity Inversion 0/1, Configuration 0/1
- **Open-drain active-low interrupt output** (INT) signals input pin state changes
- **Internal ~100 kΩ pullup** on each I/O pin (pulls high when configured as input and undriven)
- **Power-on reset** initializes all registers to defaults
- **Latched outputs** with high-current drive for directly driving LEDs (25 mA sink, 10 mA source per pin)
- **Polarity inversion** register to invert input sense without software overhead
- **No dedicated reset pin** — reset only via power cycle (POR)

(datasheet_PCA9555.pdf, p.1, p.14–15)

---

## 4. Interface Summary

| Parameter | Value | Source |
|-----------|-------|--------|
| Interface type | I2C / SMBus | datasheet_PCA9555.pdf, p.1 |
| Bus lines | SDA (data), SCL (clock) | datasheet_PCA9555.pdf, p.1 |
| Max clock frequency | 400 kHz (Fast Mode) | datasheet_PCA9555.pdf, p.1, p.7 |
| Standard Mode supported | Yes, 0–100 kHz | datasheet_PCA9555.pdf, p.7 |
| Address bits (fixed) | `0 1 0 0` (MSB first, bits [7:4] of address byte) | datasheet_PCA9555.pdf, p.19 |
| Address bits (programmable) | A2, A1, A0 (bits [3:1] of address byte) | datasheet_PCA9555.pdf, p.19 |
| R/W bit | Bit 0 of address byte: 0 = write, 1 = read | datasheet_PCA9555.pdf, p.19 |
| 7-bit address range | 0x20–0x27 (decimal 32–39) | datasheet_PCA9555.pdf, p.19 |
| General call address response | **No** — device does not respond to general call | datasheet_PCA9555.pdf, p.17 |
| Pullup resistors required | Yes, on SDA and SCL (external) | datasheet_PCA9555.pdf, p.4, p.17 |
| Pullup resistor on INT | Yes, external to VCC (open-drain output) | datasheet_PCA9555.pdf, p.4 |
| Bus capacitive load max | 400 pF | datasheet_PCA9555.pdf, p.7–8 |

### Address Table

| A2 | A1 | A0 | 7-bit Address (hex) | 7-bit Address (dec) |
|----|----|----|---------------------|---------------------|
| L | L | L | 0x20 | 32 |
| L | L | H | 0x21 | 33 |
| L | H | L | 0x22 | 34 |
| L | H | H | 0x23 | 35 |
| H | L | L | 0x24 | 36 |
| H | L | H | 0x25 | 37 |
| H | H | L | 0x26 | 38 |
| H | H | H | 0x27 | 39 |

(datasheet_PCA9555.pdf, p.19)

**CRITICAL**: Address inputs A0–A2 must **not** be changed between Start and Stop conditions.
(datasheet_PCA9555.pdf, p.17)

---

## 5. Electrical and Timing Constraints Relevant to Software

### Operating Conditions

| Parameter | Min | Max | Unit | Notes |
|-----------|-----|-----|------|-------|
| VCC supply voltage | 2.3 | 5.5 | V | — |
| Operating temperature | –40 | 85 | °C | — |
| VIH (SCL, SDA) | 0.7 × VCC | VCC | V | — |
| VIH (A0–A2, P-ports) | 0.7 × VCC | 5.5 | V | 5 V tolerant |
| VIL (SCL, SDA) | –0.5 | 0.3 × VCC | V | — |
| VIL (A0–A2, P-ports) | –0.5 | 0.3 × VCC | V | — |
| IOH per I/O pin | — | –10 | mA | Source current max |
| IOL per I/O pin | — | 25 | mA | Sink current max |

(datasheet_PCA9555.pdf, p.5)

### Addendum: Absolute Maximum Ratings

| Parameter | Symbol | Min | Max | Unit | Notes |
|-----------|--------|-----|-----|------|-------|
| Supply voltage range | VCC | –0.5 | 6 | V | — |
| Input voltage range | VI | –0.5 | 6 | V | (1) |
| Output voltage range | VO | –0.5 | 6 | V | (1) |
| Input clamp current (VI < 0) | IIK | — | –20 | mA | — |
| Output clamp current (VO < 0) | IOK | — | –20 | mA | — |
| I/O clamp current (VO < 0 or VO > VCC) | IIOK | — | ±20 | mA | — |
| Continuous output low current | IOL | — | 50 | mA | VO = 0 to VCC |
| Continuous output high current | IOH | — | –50 | mA | VO = 0 to VCC |
| Continuous current through GND | ICC | — | –250 | mA | — |
| Continuous current through VCC | ICC | — | 160 | mA | — |
| Storage temperature range | Tstg | –65 | 150 | °C | — |

(1) The input negative-voltage and output voltage ratings may be exceeded if the input and output current ratings are observed.

**Stresses beyond those listed under Absolute Maximum Ratings may cause permanent damage to the device. These are stress ratings only, and functional operation of the device at these or any other conditions beyond those indicated under Recommended Operating Conditions is not implied. Exposure to absolute-maximum-rated conditions for extended periods may affect device reliability.**
(datasheet_PCA9555.pdf, p.5)

### Addendum: Output Voltage Specifications (VOH, VOL)

**P-Port High-Level Output Voltage (VOH)**:

| IOH | VCC | VOH Min | Unit |
|-----|-----|---------|------|
| –8 mA | 2.3 V | 1.8 | V |
| –8 mA | 3 V | 2.6 | V |
| –8 mA | 4.75 V | 4.1 | V |
| –10 mA | 2.3 V | 1.7 | V |
| –10 mA | 3 V | 2.5 | V |
| –10 mA | 4.75 V | 4.0 | V |

**Low-Level Output Current (IOL) at specific VOL** (guaranteed sink current):

| Output | VOL | IOL Min | IOL Typ | IOL Max | Unit | VCC |
|--------|-----|---------|---------|---------|------|-----|
| SDA | 0.4 V | — | — | 3 | mA | 2.3–5.5 V |
| P port | 0.5 V | — | 8 | 20 | mA | 2.3–5.5 V |
| P port | 0.7 V | — | 10 | 24 | mA | 2.3–5.5 V |
| INT | 0.4 V | — | — | 3 | mA | 2.3–5.5 V |

(datasheet_PCA9555.pdf, p.7)

### Addendum: Input Diode Clamp and Leakage Currents

| Parameter | Symbol | Test Condition | VCC | Min | Typ | Max | Unit |
|-----------|--------|----------------|-----|-----|-----|-----|------|
| Input diode clamp voltage | VIK | II = –18 mA | 2.3–5.5 V | — | — | –1.2 | V |
| Input current (SCL, SDA) | II | VI = VCC or GND | 2.3–5.5 V | — | — | ±1 | μA |
| Input current (A2–A0) | II | VI = VCC or GND | 2.3–5.5 V | — | — | ±1 | μA |
| P-port input high current | IIH | VI = VCC | 2.3–5.5 V | — | — | 1 | μA |
| P-port input low current | IIL | VI = GND | 2.3–5.5 V | — | — | –100 | μA |

**Note**: The IIL of –100 μA for P-port at VI = GND is the current drawn by the internal ~100 kΩ pullup resistor when an input pin is held low. This is consistent with the ΔICC specification.
(datasheet_PCA9555.pdf, p.7)

### Current Limits

- Each I/O pin: max 25 mA sink, max 10 mA source (absolute max: 50 mA sink, 50 mA source)
- Each octal group (P00–P07 or P10–P17): max 100 mA sinking total
- Each octal group: max 80 mA sourcing total
- Device total sourcing: max 160 mA
- Device total sinking: max 200 mA (100 mA per port implied)
- Continuous current through GND: max 250 mA
- Continuous current through VCC: max 160 mA

(datasheet_PCA9555.pdf, p.5, p.7)

### Supply Current (ICC)

| Condition | VCC | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Operating (400 kHz, inputs, no load) | 5.5 V | 100 | 200 | μA |
| Operating (400 kHz, inputs, no load) | 3.6 V | 30 | 75 | μA |
| Operating (400 kHz, inputs, no load) | 2.7 V | 20 | 50 | μA |
| Standby, all inputs low | 5.5 V | 1.1 | 1.5 | mA |
| Standby, all inputs low | 3.6 V | 0.7 | 1.3 | mA |
| Standby, all inputs low | 2.7 V | 0.5 | 1 | mA |
| Standby, all inputs high | 5.5 V | 2.5 | 3.5 | μA |
| Standby, all inputs high | 3.6 V | 1 | 1.8 | μA |
| Standby, all inputs high | 2.7 V | 0.7 | 1.6 | μA |
| ΔICC (1 input at VCC−0.6 V) | 2.3–5.5 V | — | 1.5 | mA |

(datasheet_PCA9555.pdf, p.7)

**IMPORTANT**: Standby current with **low inputs** is dramatically higher (up to 1.5 mA at 5.5 V) than with high inputs (3.5 μA). This is due to the internal ~100 kΩ pullup resistor conducting current when an input is held low. For battery applications, ensure unused inputs are pulled high or configured as outputs driven high.
(datasheet_PCA9555.pdf, p.7, p.15, p.25)

### I2C Timing — Standard Mode (0–100 kHz)

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| Clock frequency | fSCL | 0 | 100 | kHz |
| Clock high time | tSCH | 4 | — | μs |
| Clock low time | tSCL | 4.7 | — | μs |
| Spike suppression | tSP | — | 50 | ns |
| Data setup time | tSDS | 250 | — | ns |
| Data hold time | tSDH | 0 | — | ns |
| Input rise time | tICR | — | 1000 | ns |
| Input fall time | tICF | — | 300 | ns |
| Output fall time | tOCF | — | 300 | ns |
| Bus free time (stop→start) | tBUF | 4.7 | — | μs |
| Start/repeated-start setup | tSTS | 4.7 | — | μs |
| Start/repeated-start hold | tSTH | 4 | — | μs |
| Stop condition setup | tSPS | 4 | — | μs |
| Valid data time (SCL low → SDA out) | tVD(DATA) | — | 3.45 | μs |
| Valid ACK time | tVD(ACK) | — | 3.45 | μs |

(datasheet_PCA9555.pdf, p.7)

### I2C Timing — Fast Mode (0–400 kHz)

| Parameter | Symbol | Min | Max | Unit |
|-----------|--------|-----|-----|------|
| Clock frequency | fSCL | 0 | 400 | kHz |
| Clock high time | tSCH | 0.6 | — | μs |
| Clock low time | tSCL | 1.3 | — | μs |
| Spike suppression | tSP | — | 50 | ns |
| Data setup time | tSDS | 100 | — | ns |
| Data hold time | tSDH | 0 | — | ns |
| Input rise time | tICR | 20 | 300 | ns |
| Input fall time | tICF | 20×(VCC/5.5V) | 300 | ns |
| Output fall time | tOCF | 20×(VCC/5.5V) | 300 | ns |
| Bus free time (stop→start) | tBUF | 1.3 | — | μs |
| Start/repeated-start setup | tSTS | 0.6 | — | μs |
| Start/repeated-start hold | tSTH | 0.6 | — | μs |
| Stop condition setup | tSPS | 0.6 | — | μs |
| Valid data time (SCL low → SDA out) | tVD(DATA) | — | 0.9 | μs |
| Valid ACK time | tVD(ACK) | — | 0.9 | μs |

(datasheet_PCA9555.pdf, p.7–8)

### Switching Characteristics (I/O Ports)

| Parameter | Symbol | From | To | Min | Max | Unit |
|-----------|--------|------|----|-----|-----|------|
| Interrupt valid time | tIV | P port | INT | — | 4 | μs |
| Interrupt reset delay | tIR | SCL | INT | — | 4 | μs |
| Output data valid | tPV | SCL | P port | — | 200 | ns |
| Input data setup time | tPS | P port | SCL | 150 | — | ns |
| Input data hold time | tPH | P port | SCL | 1 | — | μs |

(datasheet_PCA9555.pdf, p.8)

### Pin Capacitance

| Pin | Typ | Max | Unit |
|-----|-----|-----|------|
| SCL (CI) | 3 | 8 | pF |
| SDA (Cio) | 3 | 9.5 | pF |
| P port (Cio) | 3.7 | 9.5 | pF |

(datasheet_PCA9555.pdf, p.7)

### Addendum: Electrical Characteristics Footnotes

- (1) All typical values are at nominal supply voltage (2.5-V, 3.3-V, or 5-V VCC) and TA = 25°C.
- (2) Each I/O must be externally limited to a maximum of 25 mA, and each octal (P07–P00 and P17–P10) must be limited to a maximum current of 100 mA, for a device total of 200 mA.
- (3) The total current sourced by all I/Os must be limited to 160 mA (80 mA for P07–P00 and 80 mA for P17–P10).
- (4) Recommended Operating Conditions footnote: For voltages applied above VCC, an increase in ICC will result.
- (5) Switching Characteristics test condition: CL ≤ 100 pF.
  (datasheet_PCA9555.pdf, p.5, p.7, p.8)

### Addendum: Thermal Information

| Thermal Metric | DB (SSOP) | DBQ (QSOP) | DGV (TVSOP) | DW (SOIC) | PW (TSSOP) | RGE (QFN) | Unit |
|----------------|-----------|------------|-------------|-----------|------------|-----------|------|
| RθJA (junction-to-ambient) | 92.9 | 81.8 | 105.4 | 66.7 | 108.8 | 48.4 | °C/W |
| RθJC(top) (junction-to-case top) | 53.5 | 39.3 | 36.7 | 36.7 | 54 | 58.1 | °C/W |
| RθJB (junction-to-board) | 50.4 | 36.0 | 50.8 | 36.7 | 62.8 | 27.1 | °C/W |
| ψJT (junction-to-top char.) | 21.9 | 7.6 | 2.4 | 13.1 | 11.1 | 3.3 | °C/W |
| ψJB (junction-to-board char.) | 50.1 | 35.6 | 50.3 | 62.3 | 62.3 | 27.2 | °C/W |
| RθJC(bot) (junction-to-case bottom) | n/a | n/a | n/a | n/a | n/a | 15.3 | °C/W |

All packages are 24 pins.
(datasheet_PCA9555.pdf, p.6)

### Addendum: Parameter Measurement Test Conditions

- **I2C load circuit**: RL = 1 kΩ pullup to VCC, CL = 50 pF (includes probe and jig capacitance).
- **P-port load circuit**: CL = 100 pF (includes probe and jig capacitance).
- **tPV measurement**: Measured from 0.7 × VCC on SCL to 50% I/O (Pn) output.
- All inputs supplied by generators: PRR ≤ 10 MHz, ZO = 50 Ω, tr/tf ≤ 30 ns.
- Outputs are measured one at a time, with one transition per measurement.
- Cb = total capacitance of one bus line in pF (for I2C bus capacitive load spec).
  (datasheet_PCA9555.pdf, p.12–13)

---

## 6. Power, Reset, Enable, and Startup Behavior

### Power-On Reset (POR)

- **No dedicated reset pin**. Reset is achieved only via power cycling VCC.
  (datasheet_PCA9555.pdf, p.14, p.27)
- When VCC rises from 0 V, an internal POR circuit holds the device in reset until VCC reaches **VPORR** (rising threshold).
  (datasheet_PCA9555.pdf, p.15)
- **VPORR** (rising): min 1.033 V, typ not stated, max 1.428 V
  (datasheet_PCA9555.pdf, p.27)
- **VPORF** (falling): min 0.767 V, typ not stated, max 1.144 V
  (datasheet_PCA9555.pdf, p.27)
- Alternatively: VPORR typ = 1.2–1.5 V (from Electrical Characteristics table)
  (datasheet_PCA9555.pdf, p.7)
- VPORF typ = 0.75–1 V (from Electrical Characteristics table)
  (datasheet_PCA9555.pdf, p.7)
- Upon POR release: all registers reset to defaults, I2C/SMBus state machine initialized.
  (datasheet_PCA9555.pdf, p.15)
- For a subsequent power-reset cycle: VCC must drop below VPORF, then rise back to operating voltage.
  (datasheet_PCA9555.pdf, p.15)

### POR Sequencing Requirements

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| VCC fall rate | — | 1 | 100 | ms |
| VCC rise rate | 0.01 | — | 100 | ms |
| Time to re-ramp (VCC drops to GND) | 0.001 | — | — | ms |
| Time to re-ramp (VCC drops to VPOR_MIN−50mV) | 0.001 | — | — | ms |
| Glitch-safe level (VCC_GH, 1 μs glitch) | 1.2 | — | — | V |

(datasheet_PCA9555.pdf, p.27)

### Default Register State After POR

| Register | Command Byte | Default Value |
|----------|-------------|---------------|
| Input Port 0 | 0x00 | xxxx xxxx (mirrors actual pin state) |
| Input Port 1 | 0x01 | xxxx xxxx (mirrors actual pin state) |
| Output Port 0 | 0x02 | 1111 1111 (0xFF) |
| Output Port 1 | 0x03 | 1111 1111 (0xFF) |
| Polarity Inversion Port 0 | 0x04 | 0000 0000 (0x00) |
| Polarity Inversion Port 1 | 0x05 | 0000 0000 (0x00) |
| Configuration Port 0 | 0x06 | 1111 1111 (0xFF) — all inputs |
| Configuration Port 1 | 0x07 | 1111 1111 (0xFF) — all inputs |

(datasheet_PCA9555.pdf, p.19)

**Combined effect at power-on**: All I/Os are configured as inputs (Configuration = 0xFF), outputs default to high (Output = 0xFF), no polarity inversion (Polarity = 0x00). Because of the ~100 kΩ internal pullup, undriven input pins read high.
(datasheet_PCA9555.pdf, p.14–15, p.19)

---

## 7. Pin Behavior Relevant to Firmware

### Pin Table

| Pin Name | Direction | Type | Description | Notes |
|----------|-----------|------|-------------|-------|
| SDA | Bidirectional | Open-drain (I2C) | Serial data | External pullup to VCC required |
| SCL | Input | Open-drain (I2C) | Serial clock | External pullup to VCC required |
| INT | Output | Open-drain, active low | Interrupt output | External pullup to VCC required |
| A0 | Input | Digital | Address bit 0 | Connect directly to VCC or GND |
| A1 | Input | Digital | Address bit 1 | Connect directly to VCC or GND |
| A2 | Input | Digital | Address bit 2 | Connect directly to VCC or GND |
| P00–P07 | Bidirectional | Push-pull | Port 0 I/O pins | 5 V tolerant; internal ~100 kΩ pullup |
| P10–P17 | Bidirectional | Push-pull | Port 1 I/O pins | 5 V tolerant; internal ~100 kΩ pullup |
| VCC | Power | — | Supply voltage | 2.3 V to 5.5 V |
| GND | Power | — | Ground | — |

(datasheet_PCA9555.pdf, p.4)

### I/O Pin Behavior Detail

- **As input**: FETs Q1 and Q2 are off → high-impedance. Internal ~100 kΩ pullup to VCC is present.
  Input voltage may go up to 5.5 V regardless of VCC (5 V tolerant).
  (datasheet_PCA9555.pdf, p.15)
- **As output**: Push-pull structure. Q1 (pull-up to VCC) or Q2 (pull-down to GND) enabled depending on Output Port register state. Low-impedance path.
  (datasheet_PCA9555.pdf, p.15)
- Output port register default = 0xFF (high), so if a pin is switched from input to output, it will drive high initially.
  (datasheet_PCA9555.pdf, p.19)
- **CAUTION**: Changing an I/O from output to input may cause a **false interrupt** if the pin state doesn't match the Input Port register contents.
  (datasheet_PCA9555.pdf, p.16)

### INT Pin Behavior

- Open-drain, active low.
- Requires external pullup resistor to VCC.
- Asserted when any input pin state differs from its corresponding Input Port register value.
- De-asserts (goes high-impedance / pulled high) when the input port that caused the interrupt is read (at the ACK/NACK bit), or when the pin returns to its original state.

(datasheet_PCA9555.pdf, p.14, p.16)

### Address Pin Behavior

- A0, A1, A2: connect directly to VCC or GND. Input current ±1 μA.
- **Must not change** between Start and Stop conditions.
  (datasheet_PCA9555.pdf, p.4, p.7, p.17)

---

## 8. Register Map Overview

The PCA9555 has **8 registers** addressed by a 3-bit command byte (bits B2:B1:B0). Registers are organized as 4 pairs of port 0 / port 1 registers.

| Command Byte (hex) | Register Name | R/W | Default | Width |
|---------------------|---------------|-----|---------|-------|
| 0x00 | Input Port 0 | Read only | xxxx xxxx | 8 bits |
| 0x01 | Input Port 1 | Read only | xxxx xxxx | 8 bits |
| 0x02 | Output Port 0 | Read/Write | 1111 1111 (0xFF) | 8 bits |
| 0x03 | Output Port 1 | Read/Write | 1111 1111 (0xFF) | 8 bits |
| 0x04 | Polarity Inversion Port 0 | Read/Write | 0000 0000 (0x00) | 8 bits |
| 0x05 | Polarity Inversion Port 1 | Read/Write | 0000 0000 (0x00) | 8 bits |
| 0x06 | Configuration Port 0 | Read/Write | 1111 1111 (0xFF) | 8 bits |
| 0x07 | Configuration Port 1 | Read/Write | 1111 1111 (0xFF) | 8 bits |

(datasheet_PCA9555.pdf, p.19)

### Command Byte Format

```
Bit:  7   6   5   4   3   2   1   0
      0   0   0   0   0   B2  B1  B0
```

Only bits [2:0] are used. Upper bits [7:3] are always 0.
(datasheet_PCA9555.pdf, p.19)

### Register Pair Architecture

The 8 registers operate as **4 pairs**:
- Pair 0: Input Port 0 (0x00) + Input Port 1 (0x01)
- Pair 1: Output Port 0 (0x02) + Output Port 1 (0x03)
- Pair 2: Polarity Inversion Port 0 (0x04) + Polarity Inversion Port 1 (0x05)
- Pair 3: Configuration Port 0 (0x06) + Configuration Port 1 (0x07)

Auto-increment alternates **within** a pair only — it does NOT increment across pairs.
(datasheet_PCA9555.pdf, p.21; auto_increment_feature.pdf, p.4)

---

## 9. Detailed Register and Bitfield Breakdown

### 9.1 Input Port 0 (Command Byte 0x00)

| Bit | Name | Access | Default | Description |
|-----|------|--------|---------|-------------|
| 7 | I0.7 | R | X | Reflects logic level of pin P07 |
| 6 | I0.6 | R | X | Reflects logic level of pin P06 |
| 5 | I0.5 | R | X | Reflects logic level of pin P05 |
| 4 | I0.4 | R | X | Reflects logic level of pin P04 |
| 3 | I0.3 | R | X | Reflects logic level of pin P03 |
| 2 | I0.2 | R | X | Reflects logic level of pin P02 |
| 1 | I0.1 | R | X | Reflects logic level of pin P01 |
| 0 | I0.0 | R | X | Reflects logic level of pin P00 |

- Reflects incoming logic levels **regardless** of whether pin is configured as input or output.
- Writes to this register have **no effect**.
- Default "X" means value is determined by the externally applied logic level.
- If polarity inversion is enabled for a bit, the value read is the **inverted** pin state.
- Before reading, a write transaction with the command byte must be sent to set the register pointer.
- Reading this register clears the interrupt for Port 0.

(datasheet_PCA9555.pdf, p.20)

### 9.2 Input Port 1 (Command Byte 0x01)

| Bit | Name | Access | Default | Description |
|-----|------|--------|---------|-------------|
| 7 | I1.7 | R | X | Reflects logic level of pin P17 |
| 6 | I1.6 | R | X | Reflects logic level of pin P16 |
| 5 | I1.5 | R | X | Reflects logic level of pin P15 |
| 4 | I1.4 | R | X | Reflects logic level of pin P14 |
| 3 | I1.3 | R | X | Reflects logic level of pin P13 |
| 2 | I1.2 | R | X | Reflects logic level of pin P12 |
| 1 | I1.1 | R | X | Reflects logic level of pin P11 |
| 0 | I1.0 | R | X | Reflects logic level of pin P10 |

- Same behavior as Input Port 0, but for Port 1 pins.
- Reading this register clears the interrupt for Port 1.

(datasheet_PCA9555.pdf, p.20)

### 9.3 Output Port 0 (Command Byte 0x02)

| Bit | Name | Access | Default | Description |
|-----|------|--------|---------|-------------|
| 7 | O0.7 | R/W | 1 | Output value for P07 |
| 6 | O0.6 | R/W | 1 | Output value for P06 |
| 5 | O0.5 | R/W | 1 | Output value for P05 |
| 4 | O0.4 | R/W | 1 | Output value for P04 |
| 3 | O0.3 | R/W | 1 | Output value for P03 |
| 2 | O0.2 | R/W | 1 | Output value for P02 |
| 1 | O0.1 | R/W | 1 | Output value for P01 |
| 0 | O0.0 | R/W | 1 | Output value for P00 |

- Bit values only affect pins configured as **outputs** (Configuration register bit = 0).
- Bits for pins configured as inputs have no effect on pins but **are stored** and can be read back.
- **Reading** this register returns the value in the output flip-flop, **NOT the actual pin value**. To read actual pin levels, read the Input Port register.
- Default = 0xFF (all high). This means newly-configured output pins will drive high initially.

(datasheet_PCA9555.pdf, p.20)

### 9.4 Output Port 1 (Command Byte 0x03)

| Bit | Name | Access | Default | Description |
|-----|------|--------|---------|-------------|
| 7 | O1.7 | R/W | 1 | Output value for P17 |
| 6 | O1.6 | R/W | 1 | Output value for P16 |
| 5 | O1.5 | R/W | 1 | Output value for P15 |
| 4 | O1.4 | R/W | 1 | Output value for P14 |
| 3 | O1.3 | R/W | 1 | Output value for P13 |
| 2 | O1.2 | R/W | 1 | Output value for P12 |
| 1 | O1.1 | R/W | 1 | Output value for P11 |
| 0 | O1.0 | R/W | 1 | Output value for P10 |

- Same behavior as Output Port 0, but for Port 1 pins.

(datasheet_PCA9555.pdf, p.20)

### 9.5 Polarity Inversion Port 0 (Command Byte 0x04)

| Bit | Name | Access | Default | Description |
|-----|------|--------|---------|-------------|
| 7 | N0.7 | R/W | 0 | 1 = invert polarity of P07 input; 0 = original polarity |
| 6 | N0.6 | R/W | 0 | 1 = invert polarity of P06 input; 0 = original polarity |
| 5 | N0.5 | R/W | 0 | 1 = invert polarity of P05 input; 0 = original polarity |
| 4 | N0.4 | R/W | 0 | 1 = invert polarity of P04 input; 0 = original polarity |
| 3 | N0.3 | R/W | 0 | 1 = invert polarity of P03 input; 0 = original polarity |
| 2 | N0.2 | R/W | 0 | 1 = invert polarity of P02 input; 0 = original polarity |
| 1 | N0.1 | R/W | 0 | 1 = invert polarity of P01 input; 0 = original polarity |
| 0 | N0.0 | R/W | 0 | 1 = invert polarity of P00 input; 0 = original polarity |

- Applies to pins defined as **inputs** by the Configuration register.
- When bit = 1: corresponding Input Port register bit reflects the **inverted** pin level.
- When bit = 0: corresponding Input Port register bit reflects the **true** pin level.
- Default = 0x00 (no inversion).
- Writing polarity inversion may trigger an interrupt if the resulting Input Port register value changes.

(datasheet_PCA9555.pdf, p.20)

### 9.6 Polarity Inversion Port 1 (Command Byte 0x05)

| Bit | Name | Access | Default | Description |
|-----|------|--------|---------|-------------|
| 7 | N1.7 | R/W | 0 | 1 = invert polarity of P17 input; 0 = original polarity |
| 6 | N1.6 | R/W | 0 | 1 = invert polarity of P16 input; 0 = original polarity |
| 5 | N1.5 | R/W | 0 | 1 = invert polarity of P15 input; 0 = original polarity |
| 4 | N1.4 | R/W | 0 | 1 = invert polarity of P14 input; 0 = original polarity |
| 3 | N1.3 | R/W | 0 | 1 = invert polarity of P13 input; 0 = original polarity |
| 2 | N1.2 | R/W | 0 | 1 = invert polarity of P12 input; 0 = original polarity |
| 1 | N1.1 | R/W | 0 | 1 = invert polarity of P11 input; 0 = original polarity |
| 0 | N1.0 | R/W | 0 | 1 = invert polarity of P10 input; 0 = original polarity |

- Same behavior as Polarity Inversion Port 0, but for Port 1 pins.

(datasheet_PCA9555.pdf, p.20)

### 9.7 Configuration Port 0 (Command Byte 0x06)

| Bit | Name | Access | Default | Description |
|-----|------|--------|---------|-------------|
| 7 | C0.7 | R/W | 1 | 1 = P07 is input; 0 = P07 is output |
| 6 | C0.6 | R/W | 1 | 1 = P06 is input; 0 = P06 is output |
| 5 | C0.5 | R/W | 1 | 1 = P05 is input; 0 = P05 is output |
| 4 | C0.4 | R/W | 1 | 1 = P04 is input; 0 = P04 is output |
| 3 | C0.3 | R/W | 1 | 1 = P03 is input; 0 = P03 is output |
| 2 | C0.2 | R/W | 1 | 1 = P02 is input; 0 = P02 is output |
| 1 | C0.1 | R/W | 1 | 1 = P01 is input; 0 = P01 is output |
| 0 | C0.0 | R/W | 1 | 1 = P00 is input; 0 = P00 is output |

- **1 = input** (high-impedance output driver, internal pullup active).
- **0 = output** (push-pull driver active, driven by Output Port register).
- Default = 0xFF (all inputs).

(datasheet_PCA9555.pdf, p.20)

### 9.8 Configuration Port 1 (Command Byte 0x07)

| Bit | Name | Access | Default | Description |
|-----|------|--------|---------|-------------|
| 7 | C1.7 | R/W | 1 | 1 = P17 is input; 0 = P17 is output |
| 6 | C1.6 | R/W | 1 | 1 = P16 is input; 0 = P16 is output |
| 5 | C1.5 | R/W | 1 | 1 = P15 is input; 0 = P15 is output |
| 4 | C1.4 | R/W | 1 | 1 = P14 is input; 0 = P14 is output |
| 3 | C1.3 | R/W | 1 | 1 = P13 is input; 0 = P13 is output |
| 2 | C1.2 | R/W | 1 | 1 = P12 is input; 0 = P12 is output |
| 1 | C1.1 | R/W | 1 | 1 = P11 is input; 0 = P11 is output |
| 0 | C1.0 | R/W | 1 | 1 = P10 is input; 0 = P10 is output |

- Same behavior as Configuration Port 0, but for Port 1 pins.

(datasheet_PCA9555.pdf, p.20)

---

## 10. Commands and Transaction-Level Behaviors

### 10.1 Command Byte

The command byte is sent in a **write** transaction immediately after the slave address byte. It selects which register is read or written. Only bits [2:0] are meaningful. Once a command byte is sent, subsequent reads access that register (and its pair partner) until a new command byte is written.
(datasheet_PCA9555.pdf, p.19)

### 10.2 Write Transaction

**Protocol**: `[S] [slave addr + W] [ACK] [command byte] [ACK] [data byte 0] [ACK] [data byte 1] [ACK] ... [P]`

- After writing to one register, the **next data byte goes to the other register in the pair**.
- Example: write to 0x02 (Output Port 0), next byte goes to 0x03 (Output Port 1), next byte back to 0x02, etc.
- There is **no limitation** on the number of data bytes in one write transmission.
- Data takes effect (output updates) upon the rising edge of the ACK clock pulse.
- Output valid time after SCL: tPV ≤ 200 ns.

(datasheet_PCA9555.pdf, p.21)

### 10.3 Read Transaction

**Protocol**: `[S] [slave addr + W] [ACK] [command byte] [ACK] [Sr] [slave addr + R] [ACK] [data byte 0] [ACK/NACK] [data byte 1] [ACK/NACK] ... [P]`

- First, a write sets the command byte (register pointer).
- Then a repeated start (Sr) and read address begin the read.
- First byte is from the addressed register; next byte from its pair partner; alternating.
- After a restart, the stored command byte is **updated** to reference whichever register was being accessed at the restart. The original command byte is forgotten.
  - Example: command byte points to Input Port 1 (0x01); restart occurs while reading Input Port 0 → command byte changes to 0x00.
- Data is clocked into the Input Port register on the **rising edge of the ACK clock pulse**.
- No limitation on number of bytes read; final byte must receive **NACK** from master.
- Transfer can be stopped at any time by a Stop condition; data at the latest ACK phase is valid.

(datasheet_PCA9555.pdf, p.21–23)

### 10.4 Auto-Increment Behavior

**The PCA9555 auto-increment is restricted to within a register pair.** It does NOT increment through all 8 registers sequentially.

- Writing to 0x02 → next byte goes to 0x03 → next to 0x02 → 0x03 → ... (toggling within Output Port pair)
- Writing to 0x06 → next byte goes to 0x07 → next to 0x06 → 0x07 → ... (toggling within Configuration pair)
- Same for reads.
- This is by design for all TI I2C I/O expanders with 2 ports.
- Auto-increment does **not** need to be enabled; it is always active.
- To write to a different register pair, a new transaction with a new command byte is required.

(datasheet_PCA9555.pdf, p.21; auto_increment_feature.pdf, p.4–5)

**Benefit**: Continuous burst writes/reads to a register pair allow rapid toggling of both ports' data in a single transaction without re-sending the command byte.
(auto_increment_feature.pdf, p.3–5)

### 10.5 Register Pointer Persistence

Once a command byte is sent, the register pointer persists until:
- A new command byte is written, OR
- A restart occurs (in which case it updates to the register being accessed at restart time)

The command byte is sent **only** during write transmissions.
(datasheet_PCA9555.pdf, p.19, p.21)

---

## 11. Initialization and Configuration Sequences

### Recommended Initialization Sequence

No explicit initialization sequence is prescribed in the datasheet. Based on register defaults and described behaviors, a typical sequence is:

1. **Wait for POR** to complete (VCC must rise above VPORR, ~1.5 V max). No explicit delay is required beyond ensuring VCC is stable — the internal POR handles initialization.

2. **Set output port values** (if any pins will be outputs):
   - Write to 0x02 (Output Port 0) and 0x03 (Output Port 1) with desired initial output values.
   - This should be done **before** configuring pins as outputs to avoid glitches, since the default output is 0xFF (all high).

3. **Set polarity inversion** (if needed):
   - Write to 0x04 (Polarity Inversion Port 0) and 0x05 (Polarity Inversion Port 1).

4. **Configure pin directions**:
   - Write to 0x06 (Configuration Port 0) and 0x07 (Configuration Port 1).
   - 1 = input, 0 = output.

5. **Read input ports** to clear any pending interrupts:
   - Read 0x00 (Input Port 0) and 0x01 (Input Port 1).

6. **Apply interrupt errata workaround**:
   - After reading input ports, write a command byte other than 0x00 to the device (e.g., write command byte 0x02 even without data) to avoid the interrupt errata.

(Derived from datasheet_PCA9555.pdf, p.14–16, p.19–21)

### Writing Both Output Ports in One Transaction

```
[S] [0x40|A2<<3|A1<<2|A0<<1|0] [ACK] [0x02] [ACK] [Port0_data] [ACK] [Port1_data] [ACK] [P]
```

This writes Output Port 0, then auto-increments to Output Port 1.
(datasheet_PCA9555.pdf, p.21)

### Writing Both Configuration Registers in One Transaction

```
[S] [0x40|A2<<3|A1<<2|A0<<1|0] [ACK] [0x06] [ACK] [Config0_data] [ACK] [Config1_data] [ACK] [P]
```

(datasheet_PCA9555.pdf, p.21)

### Reading Both Input Ports in One Transaction

```
[S] [addr+W] [ACK] [0x00] [ACK] [Sr] [addr+R] [ACK] [Port0_data] [ACK] [Port1_data] [NACK] [P]
```

(datasheet_PCA9555.pdf, p.22)

---

## 12. Operating Modes and State Machine Behavior

### I2C State Machine

- Initialized to default state at POR.
- Can be re-initialized only by power cycling (lowering VCC below VPORF, then raising above VPORR).
- The master can reset the device in case of timeout or improper operation via the POR feature.
  (datasheet_PCA9555.pdf, p.14)

### Operating Mode

- Single operating mode: active I2C slave.
- Standby: When no I2C transactions are occurring (SCL idle), the device enters a low-power standby state automatically.
  - Standby current depends heavily on I/O pin states (see Section 5).
  (datasheet_PCA9555.pdf, p.7)

### No Explicit State Machine Described

The datasheet does not describe multi-state operational modes or a complex internal state machine beyond the I2C bus controller and register logic. The device is purely reactive: it responds to I2C transactions and updates outputs / reports inputs accordingly.

---

## 13. Measurement / Data Path Behavior

### Input Data Path

1. External signal applied to P-port pin.
2. Signal passes through **input filter** (LP filter shown in block diagram).
3. If Polarity Inversion bit = 1 for this pin, the filtered signal is **inverted**.
4. Result is latched into the **Input Port register** on a **read pulse** (rising edge of ACK clock during read).
5. Input data setup time (tPS): min 150 ns before SCL rising edge.
6. Input data hold time (tPH): min 1 μs after SCL rising edge.

(datasheet_PCA9555.pdf, p.8, p.14–15, p.23)

### Output Data Path

1. Master writes data byte to Output Port register via I2C.
2. Data is latched into the **Output Port register flip-flop** on the rising edge of the ACK clock pulse.
3. If pin is configured as output (Configuration bit = 0), the flip-flop drives Q1 (high) or Q2 (low) FET.
4. Output data valid time (tPV): max 200 ns after SCL edge.
5. Output is **latched** — it retains its value until a new write or POR.

(datasheet_PCA9555.pdf, p.8, p.15, p.21)

### Input Register Latching

- The Input Port register value is latched at the time of the **read** (specifically at the ACK clock pulse during the read transaction).
- The register reflects the **real-time** value at the instant of sampling, not a previously captured snapshot.
- Between reads, the actual pin states may change, but the Input Port register is updated fresh each time it is read.

(datasheet_PCA9555.pdf, p.20, p.22–23)

---

## 14. Interrupts, Alerts, Status, and Faults

### Interrupt (INT) Output

| Property | Value |
|----------|-------|
| Type | Open-drain, active low |
| Pullup required | Yes, external to VCC |
| Trigger | Any rising or falling edge on input-configured pins |
| Valid time after edge | tIV ≤ 4 μs |
| Reset delay after read | tIR ≤ 4 μs |

(datasheet_PCA9555.pdf, p.8, p.16)

### Interrupt Generation

- An interrupt is generated by **any rising or falling edge** of port inputs configured as inputs.
- INT is asserted when any input state **differs** from its corresponding Input Port register state.
- The interrupt persists until the condition is resolved.

(datasheet_PCA9555.pdf, p.14, p.16)

### Interrupt Clearing / Reset

Interrupt is cleared (INT de-asserts) when:
1. **Data on the port changes back** to the original setting (the state that was in the register), OR
2. **The input port that caused the interrupt is read** — reset occurs at the ACK or NACK bit after the rising edge of SCL during the read.

**CRITICAL**: Each 8-pin port is read independently. An interrupt caused by Port 0 is **NOT** cleared by reading Port 1, and vice versa.
(datasheet_PCA9555.pdf, p.16)

### Interrupt Edge Cases

- Interrupts occurring **during the ACK or NACK clock pulse** may be **lost** or very short, because the interrupt circuit is being reset during this pulse.
- Each change of I/Os after resetting is detected and generates a new INT.
- Writing to another device on the bus does **not** affect the interrupt circuit.
- A pin configured as an **output cannot cause** an interrupt.
- Changing an I/O from output to input **may cause a false interrupt** if the pin state doesn't match the Input Port register contents.

(datasheet_PCA9555.pdf, p.16)

### CRITICAL: Interrupt Errata

**BUG**: The INT output will be **improperly de-asserted** (falsely cleared) if BOTH of the following conditions occur:

1. The last command byte (register pointer) written to the device was **0x00** (Input Port 0).
   - This typically happens after a read of the input port registers, since the register pointer is left at 0x00/0x01. After a read, if no new command byte is written, it remains at 0x00.
2. **Any other slave device** on the I2C bus **acknowledges an address byte with the R/W bit set high** (a read to any other device).

**System Impact**: Can cause improper interrupt handling — the master will see the interrupt as cleared when it should still be asserted.

**Workaround**: After any read operation to the PCA9555, **write a command byte other than 0x00** (e.g., 0x02 for Output Port 0) before reading from another slave device on the bus. This is a minor software change that is compatible with other versions of this device and TI redesigns.

(datasheet_PCA9555.pdf, p.16)

---

## 15. Nonvolatile Memory / OTP / EEPROM Behavior

Not applicable to this device. The PCA9555 has no nonvolatile memory, OTP, or EEPROM. All registers reset to defaults on power cycle.

---

## 16. Special Behaviors, Caveats, and Footnotes

### 16.1 Internal Pullup Resistor

- Each I/O pin has an internal **~100 kΩ pullup** resistor to VCC (shown in schematic as "100 k" between VCC and the I/O FET structure).
- This pullup is always present (not programmable).
- When pin is configured as input and undriven, it reads high due to this pullup.
- **Difference from PCA9535**: PCA9535 lacks this internal pullup. PCA9555 is otherwise identical.
- When an input is held low, the pullup sources current through itself, causing increased standby current (ΔICC up to 1.5 mA per pin at VCC – 0.6 V).

(datasheet_PCA9555.pdf, p.14–15, p.7, p.25)

### 16.2 LED Drive Considerations

- When driving LEDs connected to VCC through a resistor (active-low LED drive), the pin voltage when LED is off will be about VCC – 1.2 V (LED forward voltage drop). This is in the "increased ICC" zone.
- **Workaround A**: Add a 100 kΩ resistor in parallel with the LED to pull the pin to VCC when LED is off.
- **Workaround B**: Use a separate, higher voltage LED supply so VCC is at least 1.2 V below the LED supply.

(datasheet_PCA9555.pdf, p.25)

### 16.3 Output-to-Input Transition Caution

- Switching a pin from output to input may cause a **false interrupt** if the external pin state doesn't match the Input Port register contents at the moment of reconfiguration.

(datasheet_PCA9555.pdf, p.16)

### 16.4 Read-Back of Output Register

- Reading the Output Port register returns the **flip-flop value**, NOT the actual pin level. To read the physical pin state, read the **Input Port** register instead.

(datasheet_PCA9555.pdf, p.20)

### 16.5 Input Port Register Reflects All Pins

- The Input Port register reflects the incoming logic level of the pin **regardless** of whether the pin is configured as input or output.

(datasheet_PCA9555.pdf, p.20)

### 16.6 Address Compatibility

- The PCA9555 uses the same address space (0x20–0x27) as PCF8575, PCF8575C, and PCF8574. Up to 8 of these in any combination may share a bus, but addresses must not collide.

(datasheet_PCA9555.pdf, p.14)

### 16.7 No General Call Support

- The PCA9555 does **not** respond to the I2C general call address.

(datasheet_PCA9555.pdf, p.17)

### 16.8 5 V Tolerant Inputs

- I/O pins (P00–P07, P10–P17) and address pins (A0–A2) can accept up to 5.5 V input regardless of VCC (down to 2.3 V). SCL and SDA are limited to VCC max for high-level input.

(datasheet_PCA9555.pdf, p.5)

### 16.9 Spike Filtering

- The device has a 50 ns spike filter on SCL/SDA (both Standard and Fast modes).
- An LP (low-pass) filter and input filter are shown on the I/O port input path in the block diagram.

(datasheet_PCA9555.pdf, p.7–8, p.14)

### 16.10 No Software Reset

- There is no I2C software reset command. The only reset mechanism is power cycling.

(datasheet_PCA9555.pdf, p.14, p.27)

### Addendum: 16.11 QFN Package Pin Mapping Differences

The QFN (RGE) package has a different pinout from the SSOP/TSSOP/TVSOP/SOIC packages. Key differences:

| Pin Name | SSOP/TSSOP/TVSOP/SOIC Pin # | QFN (RGE) Pin # |
|----------|----------------------------|------------------|
| INT | 1 | 22 |
| A1 | 2 | 23 |
| A2 | 3 | 24 |
| P00 | 4 | 1 |
| P07 | 11 | 8 |
| GND | 12 | 9 |
| P10 | 13 | 10 |
| P17 | 20 | 17 |
| A0 | 21 | 18 |
| SCL | 22 | 19 |
| SDA | 23 | 20 |
| VCC | 24 | 21 |

The QFN package also has a thermal pad on the bottom.
(datasheet_PCA9555.pdf, p.4)

### Addendum: 16.12 Layout Guidelines

- Common PCB layout practices must be followed; additional high-speed concerns like matched impedances and differential pairs are **not** a concern at I2C signal speeds.
- Avoid right angles in signal traces.
- Fan out signal traces away from each other upon leaving the IC vicinity.
- Use thicker trace widths for power and ground traces.
- **Bypass/decoupling capacitors**: Use a larger capacitor (e.g., 10 μF) for short power supply glitches and a smaller capacitor (e.g., 0.1 μF) for high-frequency ripple. Place both **as close to the PCA9555 VCC/GND pins as possible**.
- A **2-layer PCB** is feasible (top layer for signal routing, bottom as split plane for VCC/GND).
- A **4-layer PCB** is preferable for higher-density boards (signals on top/bottom, one internal ground plane, one internal power plane). Use vias next to SMD pads to connect to internal planes.
  (datasheet_PCA9555.pdf, p.29)

### Addendum: 16.13 I2C Pullup Resistor Sizing

The datasheet provides application curves (Figure 9-4 and 9-5) for pullup resistor selection:

**Maximum pullup resistance (Rp_max) vs bus capacitance (Cb)**:
- Standard Mode (fSCL = 100 kHz, tr = 1 μs): Rp_max decreases as Cb increases. At 400 pF, Rp_max ≈ 2.5 kΩ. At low Cb, Rp_max can be > 20 kΩ.
- Fast Mode (fSCL = 400 kHz, tr = 300 ns): More restrictive. At 400 pF, Rp_max ≈ 1 kΩ. At low Cb, Rp_max ≈ 5 kΩ.

**Minimum pullup resistance (Rp_min) vs pullup reference voltage (VCC)**:
- When VCC > 2 V: VOL = 0.4 V, IOL = 3 mA → Rp_min = (VCC – 0.4) / 3 mA.
- When VCC ≤ 2 V: VOL = 0.2 × VCC, IOL = 2 mA → Rp_min = (VCC – 0.2 × VCC) / 2 mA.
- At VCC = 5 V: Rp_min ≈ 1.5 kΩ.
- At VCC = 3.3 V: Rp_min ≈ 1 kΩ.
  (datasheet_PCA9555.pdf, p.26)

### Addendum: 16.14 Typical Application Circuit Details

The typical application circuit (Figure 9-1) shows:
- **Pullup resistors**: 10 kΩ on SDA, SCL, and INT lines (to VCC = 5 V).
- **LED resistor**: 2 kΩ between VCC and I/O pin (for active-low LED drive).
- **Address config**: A2 = H, A1 = L, A0 = L → device address 0x24.
- **Pin allocation**: P00, P02, P03 as outputs; P01, P04–P07, P10–P17 as inputs.
- Subsystem connections: temperature sensor, counter, alarm, keypad, and controlled switch (e.g., CBT device) connected to various I/O pins.
- INT output drives a microcontroller interrupt input.
- **Design parameters**: VCC = 5 V, IOL = 25 mA, SCL speed = 400 kHz.
  (datasheet_PCA9555.pdf, p.24–25)

---

## 17. Recommended Polling and Control Strategy Hints from the Docs

### Interrupt-Driven Input Reading

The datasheet explicitly describes using INT to signal the master that an input state has changed, avoiding the need to poll over I2C:

> "By sending an interrupt signal on this line, the remote I/O can inform the microcontroller if there is incoming data on its ports without having to communicate via the I2C bus."
> (datasheet_PCA9555.pdf, p.14)

**Recommended strategy**:
1. Connect INT to a microcontroller GPIO interrupt input.
2. On INT assertion (falling edge), read the input port(s) to determine which pin(s) changed and clear the interrupt.
3. After reading, apply the **interrupt errata workaround**: write a command byte ≠ 0x00.

### Efficient Burst I/O with Auto-Increment

- Use burst writes to update both ports in a single I2C transaction:
  - Write command byte 0x02, then Port 0 data and Port 1 data in succession.
- For fast output toggling, use the auto-increment loop behavior: after setting command byte to 0x02, continuous writes alternate between Port 0 and Port 1 without re-sending the command byte. This minimizes latency.
  (auto_increment_feature.pdf, p.4–5)
- For fast input polling of a single port, the auto-increment loops within the pair, so continuous reads return alternating Port 0 / Port 1 data.
  (auto_increment_feature.pdf, p.5)

### Minimizing Standby Current

- For battery-powered applications, ensure all unused input pins are pulled high (externally or using the internal pullup which is already present). Holding inputs low dramatically increases supply current.
- Consider configuring unused pins as outputs driven high.

(datasheet_PCA9555.pdf, p.7, p.25)

---

## 18. Ambiguities, Conflicts, and Missing Information

### 18.1 VPORR Discrepancy

- The Electrical Characteristics table (p.7) lists VPORR as min=1.2 V, typ not stated, max=1.5 V.
- The Power Supply Recommendations table (p.27) lists VPORR as min=1.033 V, max=1.428 V.
- These ranges overlap but are not identical. The p.27 values appear more precise (likely production-tested). **Use p.27 values for design margin.**

### 18.2 VPORF Discrepancy

- Electrical Characteristics (p.7): VPORF typ range 0.75–1 V.
- Power Supply table (p.27): VPORF min=0.767 V, max=1.144 V.
- Similar overlap issue. **Use p.27 values for design margin.**

### 18.3 Input Port Register Latch Timing

- The datasheet states data is "clocked into the register on the rising edge of the ACK clock pulse" in the read section (p.21–22). However, the Input Port register is also described as reflecting incoming logic levels (p.20). The exact mechanism: the input is sampled/latched during the read operation at the ACK. Between reads, the register is not static — it updates continuously, but the value captured and sent to the master is the state at the ACK.
- This is somewhat ambiguous. The safest interpretation: the Input Port register is a transparent latch that is gated/sampled during the read sequence.

### 18.4 Polarity Inversion Effect on Interrupts

- The datasheet does not explicitly state whether changing the Polarity Inversion register bits triggers an interrupt when the effective Input Port value changes as a result. It is implied (since the interrupt compares I/O state to the Input Port register), but not explicitly confirmed.

### 18.5 Command Byte Bits [7:3]

- The datasheet shows bits [7:3] of the command byte as always 0, but does not state what happens if non-zero values are written. Behavior is undefined for command byte values > 0x07.

### 18.6 Power-On Reset Duration

- No explicit POR delay time is specified. The datasheet only specifies voltage thresholds and ramp rates. The time from VCC crossing VPORR to the device being fully operational is not stated.

### 18.7 Write to Input Port Register

- Stated that writes to Input Port registers "have no effect" (p.20), but it is not stated whether they are ACKed or NACKed. They are presumably ACKed but discarded (standard for I2C devices).

### 18.8 Glitch Width (VCC_GW) Max Value

- The VCC_GW max value is blank in Table 10-1 (p.27). Only the parameter definition is given; no numeric value is provided.

### 18.9 SMBus Compliance Level

- The device is described as compatible with "I2C and SMBus" but the specific SMBus compliance level (1.0, 1.1, 2.0, etc.) is not stated.

### Addendum: 18.10 Machine Model ESD Discrepancy

- The Features list (p.1) claims ESD protection for "200-V Machine Model (A115-A)", but the formal ESD Ratings table (p.5, Section 6.2) lists only HBM (2000 V) and CDM (1000 V). Machine Model is not included in the ESD Ratings table, possibly because it was deprecated from JEDEC standards after the original datasheet revision.
  (datasheet_PCA9555.pdf, p.1, p.5)

### Addendum: 18.11 Revision History — Implementation-Relevant Changes

Key changes across datasheet revisions that may affect designs based on older revisions:
- **Rev I → Rev J (March 2021)**: VIH max for SCL/SDA changed from 5.5 V to VCC. Ci SCL max changed from 7 pF to 8 pF. Cio SDA max changed from 7 pF to 9.5 pF. VPORF row added. ICC standby high inputs values changed. Power supply recommendations changed.
- **Rev E → Rev F (June 2014)**: Interrupt Errata section was added.
  (datasheet_PCA9555.pdf, p.2–3)

---

## 19. Raw Implementation Checklist

- [ ] **Hardware address**: Set A0, A1, A2 pins to VCC or GND; connect directly (no floating).
- [ ] **I2C pullups**: Add external pullup resistors on SDA and SCL. Typical values: 2.2 kΩ to 10 kΩ depending on bus capacitance and speed.
- [ ] **INT pullup**: Add external pullup on INT line (10 kΩ typical).
- [ ] **Bypass capacitor**: Place 0.1 μF (min) and optionally 10 μF capacitor close to VCC/GND pins.
- [ ] **POR wait**: Ensure VCC is stable above VPORR (max 1.5 V) before first I2C transaction. No explicit delay documented — just ensure supply is stable.
- [ ] **Verify communication**: Read any register (e.g., Configuration Port 0 at 0x06, expect 0xFF) to confirm I2C connectivity.
- [ ] **Set output values before configuring as outputs**: Write Output Port registers (0x02, 0x03) to desired values before writing Configuration registers (0x06, 0x07) with 0 bits to avoid transient glitches.
- [ ] **Configure polarity inversion** if needed: Write to 0x04, 0x05.
- [ ] **Configure pin directions**: Write to 0x06, 0x07. Bit=1 → input, Bit=0 → output.
- [ ] **Clear pending interrupts**: Read Input Port 0 (0x00) and Input Port 1 (0x01).
- [ ] **Apply interrupt errata workaround**: After reading input ports, write a command byte ≠ 0x00 (e.g., dummy write of command byte 0x02) before communicating with other devices on the bus.
- [ ] **Service interrupts**: On INT falling edge, read the relevant Input Port register(s). Both ports must be read separately to clear interrupts from each port.
- [ ] **Re-apply errata workaround** after each input port read operation.
- [ ] **Output read-back**: To verify output pin states, read the Input Port register (not the Output Port register, which returns the flip-flop value, not the actual pin).
- [ ] **Handle output-to-input transitions**: Be aware a false interrupt may occur when changing Configuration bits from 0 to 1.
- [ ] **Burst operations**: Use auto-increment (within register pairs) for efficient 2-byte reads/writes to both ports in one transaction.
- [ ] **Do not float address pins**: A0, A1, A2 must be connected to VCC or GND.
- [ ] **Current budget**: Ensure total sink current per port ≤ 100 mA, total source per port ≤ 80 mA, device total source ≤ 160 mA.
- [ ] **Battery applications**: Minimize standby current by ensuring input pins are at VCC level (not floating low or driven low) when possible.
- [ ] **No software reset available**: If device enters an unknown state, the only recovery is power cycling VCC below VPORF then back up.

---

## 20. Source Citation Appendix

All facts in this document are extracted from the following two source documents:

| Short Reference | Full Document Title | TI Document Number | Revision | Date |
|-----------------|--------------------|--------------------|----------|------|
| datasheet_PCA9555.pdf | PCA9555 Remote 16-bit I2C and SMBus I/O Expander with Interrupt Output and Configuration Registers | SCPS131J | Rev. J | August 2005, revised March 2021 |
| auto_increment_feature.pdf | I2C: What is the Auto Increment Feature? | SLVAFL0 | — | July 2024 |

### Page-Level Citation Index

| Topic | Source | Pages |
|-------|--------|-------|
| Features, package info, block diagram | datasheet_PCA9555.pdf | 1 |
| Pin configuration and functions | datasheet_PCA9555.pdf | 4 |
| Absolute maximum ratings | datasheet_PCA9555.pdf | 5 |
| Recommended operating conditions | datasheet_PCA9555.pdf | 5 |
| Thermal information | datasheet_PCA9555.pdf | 6 |
| Electrical characteristics | datasheet_PCA9555.pdf | 7 |
| I2C timing (standard and fast mode) | datasheet_PCA9555.pdf | 7–8 |
| Switching characteristics | datasheet_PCA9555.pdf | 8 |
| Parameter measurement info | datasheet_PCA9555.pdf | 12–13 |
| Overview, block diagram, device features | datasheet_PCA9555.pdf | 14–15 |
| Interrupt behavior and errata | datasheet_PCA9555.pdf | 16 |
| I2C interface protocol | datasheet_PCA9555.pdf | 17 |
| Register map, address byte, command byte | datasheet_PCA9555.pdf | 18–19 |
| Register descriptions (all 8 registers) | datasheet_PCA9555.pdf | 20 |
| Bus transactions (write/read sequences) | datasheet_PCA9555.pdf | 21–23 |
| Typical application, design requirements | datasheet_PCA9555.pdf | 24–26 |
| Power supply, POR requirements | datasheet_PCA9555.pdf | 27–28 |
| Layout guidelines | datasheet_PCA9555.pdf | 29 |
| Orderable information | datasheet_PCA9555.pdf | 31+ |
| Auto-increment concept and benefits | auto_increment_feature.pdf | 1–3 |
| Auto-increment within I/O expander pairs | auto_increment_feature.pdf | 4 |
| Auto-increment enable requirements | auto_increment_feature.pdf | 5 |

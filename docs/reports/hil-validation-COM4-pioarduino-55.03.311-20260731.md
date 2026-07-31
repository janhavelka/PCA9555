# COM4 pioarduino 55.03.311 Hardware Validation Summary — 2026-07-31

Target: ESP32-S3 QFN56 revision 0.1 on `COM4`, native USB Serial/JTAG, 4 MB
embedded flash, 2 MB embedded PSRAM, and PCA9555 address `0x20`. The Arduino
example used SDA GPIO8, SCL GPIO9, 400 kHz I2C, 50 ms adapter timeout, and
115200-baud CLI framing with DTR/RTS deasserted before opening the port.

Runtime and upload evidence:

- pioarduino `platform-espressif32` `55.03.311`;
- Arduino-ESP32 `3.3.11` and ESP-IDF `v5.5.5`, reported by the flashed firmware;
- PlatformIO Core `6.1.19`, GCC `14.2.0+20260121`, and esptool `5.3.0`;
- esptool identified the target, auto-detected 4 MB flash, wrote the combined
  firmware image, and verified its hash;
- library `3.0.0`, source revision `fb54fcf` plus the documented dirty upgrade
  changes under test.

## Automated Results

- Read-oriented smoke: 14/14 commands passed. Scan found four responding bus
  addresses including `0x20`; probe, paired/scalar reads, register dump, pin
  summary, and 10 input-read/pointer-park cycles passed. Initial captured health
  was READY with 40 tracked successes and zero failures.
- Built-in full plan: 19/19 commands passed. The guarded API self-test reported
  50 pass / 0 fail / 0 skip. Read stress completed 1,000/1,000, mixed
  read/write/configuration/polarity/mask stress completed 100/100, and both
  destructive phases were followed by successful complete-image recovery.
- Extended command plan: 72/72 commands passed, including 24 successful
  complete-image recoveries. Coverage included Port 1 and pin 15 boundary
  accessors, all eight scalar register reads, all four odd-start paired-register
  wrap cases, pin/port/direction/polarity/mask operations, raw output/polarity
  register writes, exact output patterns, all-high/all-low, a 32/32 accumulating
  sweep, and a 16/16 walking-one test.
- Final health was READY and bound, with zero consecutive failures, 2,644 total
  tracked successes, zero total failures, and no last error. The final explicit
  recovery applied and verified high output latches, normal polarity, and all
  pins configured as inputs.

The ignored machine summaries are under `hil_logs/i2c_20260731_155046/`,
`hil_logs/i2c_20260731_155507/`, `hil_logs/i2c_20260731_155524/`, and
`hil_logs/i2c_20260731_155647/`. They retain bounded classifier evidence, not
raw serial transcripts.

## Scope And Open Physical Checks

The runner verdict remains `OPERATOR_REVIEW_REQUIRED`. These runs prove the
automated command/register/API behavior on the connected fixture, but they do
not close the following operator or instrumentation gates:

- photographs, supply/pull-up/load values, and independent fixture review;
- externally stimulated observations for every input and externally measured,
  current-limited observations for every output;
- Port 0, Port 1, and simultaneous INT assertion/clear captures;
- logic-analyzer proof of latch-before-direction ordering and the mandatory
  nonzero pointer park after input reads;
- a true PCA9555 power cycle for POR-default checking;
- wrong-address, disconnect/reconnect, ambiguous transfer, brownout, and bus
  recovery tests;
- 100 kHz, shared-bus, ESP32-S2 hardware, and native ESP-IDF hardware runs.

An ACK at `0x20` is address-response evidence only. The PCA9555 has no
documented identity register, so this run does not claim chip identity.

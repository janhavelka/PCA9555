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
- library `3.0.1`, release candidate based on source revision `e618f5c` plus the
  synchronized `v3.0.1` release metadata under test.

## Automated Results

- Read-oriented smoke: 14/14 commands passed. Scan found four responding bus
  addresses including `0x20`; probe, paired/scalar reads, register dump, pin
  summary, and 10 input-read/pointer-park cycles passed. Initial captured health
  was READY with 40 tracked successes and zero failures.
- Built-in full plan: 19/19 commands passed. The guarded API self-test reported
  50 pass / 0 fail / 0 skip. Read stress completed 1,000/1,000, mixed
  read/write/configuration/polarity/mask stress completed 100/100, and both
  destructive phases were followed by successful complete-image recovery.
- Release-candidate full and fault plan: 46/46 automated results passed (45 serial
  commands plus one derived invariant). This reran the framework/version,
  read, self-test, 1,000-cycle read stress, 100-cycle mixed stress, and recovery
  coverage on the newly flashed CLI. The fault option then passed 22 exact
  rejection cases plus before/after health and settings snapshots. In
  particular, confirmed raw Configuration starts `6` and `7` produced the
  corrected `wreg`/`wregs <2-5>` usage rejection on target.
- Extended command plan was rerun on the newly flashed release-candidate firmware:
  72/72 commands passed, including 24 successful complete-image recoveries.
  Coverage included Port 1 and pin 15 boundary
  accessors, all eight scalar register reads, all four odd-start paired-register
  wrap cases, pin/port/direction/polarity/mask operations, raw output/polarity
  register writes, exact output patterns, all-high/all-low, a 32/32 accumulating
  sweep, and a 16/16 walking-one test.
- Final health was READY and bound, with zero consecutive failures, 2,604 total
  tracked successes, zero total failures, and no last error. The final explicit
  recovery successfully applied high output latches, normal polarity, and all
  pins configured as inputs. The fault block left every reported health and
  settings field—including address `0x20`, 50 ms timeout, shadow-valid mask
  `0x0E`, and uncertain mask `0x00`—exactly unchanged at 2,245 tracked
  successes and zero failures; the subsequent extended plan raised the final
  success count to 2,604.

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
- wrong-address, disconnect/reconnect, stuck-bus, real timeout/cancellation
  cleanup, ambiguous transfer, brownout, and bus-recovery tests;
- 100 kHz and shared-bus runs.

An ACK at `0x20` is address-response evidence only. The PCA9555 has no
documented identity register, so this run does not claim chip identity.

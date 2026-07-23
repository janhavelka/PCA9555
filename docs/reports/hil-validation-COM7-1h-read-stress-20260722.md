# COM7 Hardware Validation Summary — 2026-07-22

Target: ESP32-S3 revision 0.1 on `COM7`, native USB Serial/JTAG, PCA9555 at
`0x20`, 115200 baud. Firmware was built from `main` at
`8f4e1571abb3b38967c808e4e29da107a699008d` with the reviewed working-tree
changes listed in this task.

## Automated Result

The final stress used one continuous serial session and one device-side command
to avoid treating native-USB command framing as chip behavior.

| Evidence | Result |
| --- | ---: |
| Device-reported duration | 4,402,034 ms (73 min 22 s) |
| Input read / interrupt-clear operations | 15,000,000 passed, 0 failed |
| Tracked transfers | 30,000,000 succeeded, 0 failed |
| Required pointer-park cleanups | 15,000,000 |
| Serial reopens / retries | 0 / 0 |

Runner classification: `PASS`. Overall report disposition remains
`OPERATOR_REVIEW_REQUIRED` because the physical checks below were not supplied.
The ignored machine summary is under `hil_logs/i2c_20260722_210101/`; no long
serial transcript was retained.

## Supporting Coverage

- Full COM7 qualification: 19/19 commands passed, including register reads,
  complete-image recovery, read-only stress, mutating self-test, and mixed
  output/direction/polarity stress.
- Focused self-test framing run: 50 self-tests plus 50 recoveries passed
  100/100. A mixed diagnostic sequence passed 150/150 commands.
- Three-minute aggregate preflight: 1,953 commands, zero failures, zero serial
  reopens.
- Native driver suite: 64/64. Clean-package consumer, strict Doxygen, generated
  metadata, framework guards, and Arduino ESP32-S2/S3 builds passed.
- TunnelMonitor-node baseline: 1,086/1,086 native tests and the
  `tunnelmonitor_wifi` production build passed. The PCA9555 package consumer
  additionally proved the product-style 5 ms terminal callback, one callback
  per owner poll, exactly-once result, injected-NACK/no-retry, DEGRADED, and
  verified-recovery contracts.

## Corrections Made During HIL

- Destructive aggregate steps now run their declared complete-image recovery
  before a failure limit can stop the runner.
- Command timeout overrides can no longer shorten reviewed safe bounds.
- Known serial responses require command-specific evidence before accepting a
  prompt, preventing a delayed USB prompt from shifting response ownership.
- Normal self-test and stress output is compact and has plain-text result tails;
  verbose mode retains detailed diagnostics and all failures remain visible.
- Aggregate reports retain only the first bounded anomaly, not CLI transcripts.

## Still Open

No wiring photo, per-pin external stimulus, current-limited output observation,
INT waveform/clear capture, logic-analyzer proof of the errata pointer park,
physical disconnect/brownout recovery, 100/400 kHz comparison, shared-bus soak,
ESP32-S2 HIL, or native ESP-IDF HIL was performed. Opening this board's native
USB port resets it, so this run proves one uninterrupted open command session,
not an independently instrumented reset/uptime claim. An ACK at `0x20` proves
address response only; the PCA9555 has no chip-ID register.

TunnelMonitor-node does not currently admit a PCA9555 production dependency or
define its address, channel map, and product-safe `RegisterImage`. Those product
facts are required before adding the private adapter or claiming integrated
TunnelMonitor hardware validation.

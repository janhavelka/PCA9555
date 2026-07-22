# HIL Validation Summary - 2026-06-25

Scope: historical `v2.0.0`-era bench evidence. This is not v3 validation.

Status at the time: pre-production candidate; continuous HIL was blocked.

The durable result from the June 2026 COM5 hardware runs is that the PCA9555
driver and I2C transactions did not show a confirmed chip-level failure, but
the serial validation channel was not stable enough to support a production
claim.

Key evidence:

- Fast full-function run: `PASS=3316`, `TIMEOUT=1`, elapsed `350.828 s`,
  serial reopens `0`. Timeout occurred after `read` while waiting for `outputs`.
- Slow no-reopen run: `PASS=2452`, `TIMEOUT=1`, elapsed `2468.532 s`,
  serial reopens `0`. Timeout occurred inside `stress_mix 100 confirm`.
- Segmented 4-hour run: `PASS=13694`, failures `0`, duration `14400.015 s`,
  serial reopens `119`. This proves many short prompt-gated sessions, not one
  continuous firmware uptime window, because reopening COM5 reset the target.
- Post-segment direction-only cleanup passed: `dirin 0xFFFF confirm`, final
  health `READY`, `consecutiveFailures=0`, `totalFailures=0`. That command did
  not restore or verify output latches and polarity, so it is not evidence of a
  complete writable-state restore.
- Direct reset probe: after `read` and `outputs`, health success count reached
  `3`; closing and reopening COM5 returned health success count to `0`.

Recorded validation-channel blocker:

The blocker was the ESP32-S3 native USB CDC / COM5 command channel. Opening the
serial port with the recorded line settings reset or restarted the firmware, and
long no-reopen sessions still timed out on the command channel. The segmented
pass is therefore not valid continuous HIL evidence.

Current implication:

- Do not claim production-grade, field-validated, or fully hardware-validated
  status from these runs.
- `hil_logs/` remains ignored and was treated as transient local test output.
- The next production HIL pass should use a non-resetting UART/USB-serial path
  or another command channel that does not reset the target on reconnect, and it
  should record reset count, uptime, and heap/health telemetry in the firmware
  transcript.

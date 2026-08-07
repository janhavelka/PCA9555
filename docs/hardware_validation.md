# Hardware Validation

Automated ESP32-S3/PCA9555 upgrade HIL was performed on COM4 on 2026-07-31 with
pioarduino `55.03.311`. The reviewed summary at
`docs/reports/hil-validation-COM4-pioarduino-55.03.311-20260731.md` records
46/46 post-fix full/fault results and 72/72 extended commands passing, final
READY health, 2,604 tracked successes, and zero failures while retaining the
physical gates below.

Hardware qualification for this release is scoped to Arduino ESP32-S3.
ESP32-S2 and native ESP-IDF remain build/contract targets and are not hardware
gates; the S3 evidence does not validate native ESP-IDF runtime behavior.
Earlier COM7 HIL on 2026-07-22 completed a 73-minute device-side
input-read/pointer-park stress with 15,000,000 operations and zero failures.
That historical supporting result remains in the v3.0.1 changelog and Git
history; it is not electrical, analyzer, reset, or shared-bus evidence. An I2C
ACK proves address response only; the PCA9555 has no documented chip-ID
register.

All real-target gates below remain open. Store temporary runner output under
`hil_logs/`; Git ignores that directory. Commit only a short reviewed summary
when a run closes a gate.

## Known Constraint

The June 2026 v2-era ESP32-S3 run used native USB CDC. Opening COM5 reset the
target, and long sessions also timed out, so the run did not prove continuous
firmware uptime. Use a non-resetting UART or another stable command channel for
the strongest uptime evidence. The runner keeps one serial session open by
default and correlates each known response with command-specific evidence so a
delayed prompt cannot be mistaken for the next command. Opening the native USB
CDC port can still reset some boards; do not reopen it during an uptime claim.
The runner configures DTR/RTS before opening the port, then sends a leading
newline and the read-only `health` command to establish fresh CLI framing
without touching I2C or changing PCA9555 state.

## Run A Target

Before driving pins, record the commit, board and framework, serial channel,
PCA9555 marking and address straps, supply voltage, I2C speed, pull-ups, INT
wiring, fixture, and current limits. Do not externally drive a pin configured
as an output. The PCA9555 has no software reset; POR testing requires a real
power cycle.

Build and flash the appropriate example, then install the runner dependency:

```text
python -m platformio run -e esp32s2dev
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s3dev -t upload
python -m pip install pyserial
```

The runner does not flash firmware. Its default pass is read-oriented:

```text
python tools/run_i2c_hil.py --port <PORT> --baud 115200 --address 0x20
```

Use `--dry-run` to inspect the plan and `--parser-self-test` to test the host
classifier. Opt in deliberately with `--include-output-tests`, `--include-soak`,
or `--include-fault-tests`; mutating CLI commands still require `confirm`.
`--include-fault-tests` runs bus-silent CLI guard checks for invalid arguments,
unknown commands, and missing confirmations, then proves the bracketing health
and all reported settings fields are unchanged. It does not inject I2C, wiring,
power, timeout, or brownout faults; those remain physical operator tests.
`--timeout-s` sets a minimum only and never shortens a command's reviewed
bound. During aggregate runs, every destructive command with recovery metadata
is followed immediately by `recover confirm`, even when the primary command
failed or reached the failure limit. That command applies the example fixture
image (latches high, normal polarity, all inputs), not a universal product-safe
image.

The default command contract is:

<!-- HIL_COMMAND_SEQUENCE_START -->
- `version`
- `help`
- `scan`
- `probe`
- `settings`
- `read`
- `outputs`
- `config`
- `polarity`
- `dump`
- `pins`
- `health`
- `stress 10`
- `health`
<!-- HIL_COMMAND_SEQUENCE_END -->

For a bounded eight-hour soak on a stable serial channel:

```text
python tools/run_i2c_hil.py --port <PORT> --soak-duration-s 28800 --soak-command-mix read,outputs,config,polarity,health,probe --report docs/reports/hil-validation-<TARGET>-YYYYMMDD.md
```

The runner writes concise Markdown and JSON summaries, not full CLI
transcripts. Manual rows remain `OPERATOR_CHECK_REQUIRED`, and a serial PASS
remains `OPERATOR_REVIEW_REQUIRED`, until the checks below have attached
evidence.

## Open Gates

| Gate | Required evidence | Status |
| --- | --- | --- |
| Address, POR, and pin map | Intended addresses `0x20`-`0x27`; true-POR writable defaults; released/high and pulled-low observations for all 16 inputs | PARTIAL — `0x20` ACK/read evidence only |
| Output safety | Current-limited all-pin output observations, masked writes, and logic-analyzer proof that latches are preloaded before direction changes | PARTIAL — API/readback and sweep/walk only |
| Polarity and INT | Polarity observations; Port 0, Port 1, and both-port INT assertion/clear captures; application re-read/debounce behavior | PARTIAL — register/read stress only |
| Errata cleanup | I2C decode proving the nonzero pointer park follows input reads without another target transaction interleaving | PARTIAL — 15M successful cleanups, no analyzer capture |
| Fault and reconciliation | Wrong address, disconnect/reconnect, cancellation/timeout cleanup, ambiguous write, PCA9555-only brownout, and caller-owned complete-image apply/verify | PARTIAL — native transport injection, S3 fail-closed CLI guards, and HIL complete-image recovery only |
| Bus speeds and sharing | 100 kHz and 400 kHz runs plus a long soak with another active target through the real bus owner | PARTIAL — 400 kHz single-target S3 command/stress evidence only |
| Review package | Setup record, wiring evidence, concise runner summary, analyzer captures, reset/uptime telemetry, and reviewer sign-off | PARTIAL — concise runner summaries only |

Do not claim production-grade, field-proven, or fully hardware-validated status
until every applicable row is complete and reviewed.

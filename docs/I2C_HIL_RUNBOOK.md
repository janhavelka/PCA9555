# PCA9555 I2C HIL Runbook

This runbook describes how to use the host-side Python runner against the
existing Arduino serial CLI in `examples/01_basic_bringup_cli`.

No physical HIL validation was performed while creating this runbook. A dry run
or generated command plan is not hardware evidence. I2C ACK proves address
response only; it does not prove PCA9555 chip identity because the PCA9555 has
no documented chip ID register.

## Hardware Preflight

Record these fields before running hardware:

| Field | Value |
| --- | --- |
| Operator | |
| Date/time | |
| Branch | |
| Commit hash | |
| Worktree dirty/clean state | |
| MCU board | |
| Framework | Arduino / ESP-IDF / other |
| Build target | |
| Serial port | |
| Baud rate | 115200 |
| PCA9555 I2C address | 0x20 unless changed |
| I2C bus speed | |
| Supply voltage | |
| Pull-up values | |
| Reset wiring | Power-cycle only; PCA9555 has no software reset |
| Interrupt pin wiring | |
| Device/module model | |
| Chip marking | |
| Fixture details | |
| Firmware version | |
| Evidence directory | `hil_logs/i2c_<timestamp>/` |

## Build And Monitor Commands

Build the Arduino examples without guessing a port:

```bash
python -m platformio run -e esp32s2dev
python -m platformio run -e esp32s3dev
```

Upload only after selecting the correct target and serial port:

```bash
python -m platformio run -e esp32s2dev --target upload --upload-port <PORT>
python -m platformio run -e esp32s3dev --target upload --upload-port <PORT>
```

Manual monitor command:

```bash
python -m platformio device monitor --port <PORT> --baud 115200
```

Run the Python HIL runner:

```bash
python tools/run_i2c_hil.py --port <PORT> --baud 115200 --address 0x20
```

Dry-run planning command:

```bash
python tools/run_i2c_hil.py --dry-run
```

The runner never flashes firmware automatically. Install serial support with:

```bash
python -m pip install pyserial
```

Custom command files passed with `--commands <file>` are still safety-gated.
Commands matching the known output/configuration/fault mutation surface, such as
`pattern`, `allhigh`, `alllow`, `wpin`, `wport`, `dir`, `wreg`, `selftest`,
`stress_mix`, or `recover`, require the same explicit opt-in flags as built-in
commands.

## Default Executable Command Sequence

The default sequence is read-oriented and avoids output-driving commands.

<!-- HIL_COMMAND_SEQUENCE_START -->
- `version`
- `help`
- `scan`
- `probe`
- `cfg`
- `read`
- `outputs`
- `config`
- `polarity`
- `dump`
- `pins`
- `drv`
- `stress 10`
- `drv`
<!-- HIL_COMMAND_SEQUENCE_END -->

Expected serial results:

| Command | Expected serial result |
| --- | --- |
| `version` | Version section with PCA9555 library version |
| `help` | CLI help text |
| `scan` | `Scan complete` and the expected address, normally `0x20` |
| `probe` | `Status: OK`; ACK only, not chip identity |
| `cfg` | Settings snapshot |
| `read` | Input port section and combined value |
| `outputs` | Output latch section and combined value |
| `config` | Configuration section |
| `polarity` | Polarity inversion section |
| `dump` | Register dump |
| `pins` | Per-pin summary |
| `drv` | Driver health, current state READY |
| `stress 10` | Stress results with `fail=0` |

## Opt-In And Manual Checks

These checks must not be treated as serial-only PASS results:

| Check | Default handling | Reason |
| --- | --- | --- |
| `selftest` | `SKIPPED_UNSAFE` unless `--include-output-tests` | Mutates output latches, direction, and polarity before restoring |
| `stress_mix 100` | `SKIPPED_UNSAFE` unless `--include-output-tests` | Runs mixed read/write/config/polarity operations |
| `stress 1000` | `SKIPPED_UNSAFE` unless `--include-soak` | Longer soak gate |
| Output patterns, `allhigh`, `alllow`, `walk`, `sweep` | Manual/operator only | Drives physical pins and attached loads |
| Fault injection, disconnects, brownout | Manual/operator only | Requires safe handling and bench controls |
| INT behavior and errata pointer park | OPERATOR_CHECK_REQUIRED | Requires wiring, logic analyzer, or MCU capture |

Use current-limited loads before any output-driving test. Do not externally
drive a PCA9555 pin while it is configured as an output.

For opt-in mutating commands that declare a recovery action, the runner inserts
the recovery command into the execution plan. The current restore-safe-state
command is `dirin 0xFFFF`.

Even if all serial commands classify as PASS, the runner's final verdict remains
`OPERATOR_REVIEW_REQUIRED` until manual wiring, physical output, INT, errata,
and fault/recovery evidence is reviewed.

## Evidence Capture

For every real hardware run, keep:

- `serial_transcript.txt`
- `summary.md`
- `summary.json`
- `operator_checklist.md`
- board and wiring photos
- logic analyzer capture for INT and errata checks when applicable
- scope or meter readings for supply, pull-ups, and output loads
- operator notes explaining any `REVIEW_REQUIRED`, `SERIAL_OK_OR_REVIEW`, or
  `OPERATOR_CHECK_REQUIRED` item

## Known Limits

The CLI starts the driver once during firmware setup. Wrong-address `begin()`
validation therefore requires rebuilding or changing firmware/fixture settings,
not only typing an interactive command.

`scan` and `probe` are useful bus diagnostics, but ACK proves address response
only. The PCA9555 has no identity register, so chip identity requires fixture
control and operator evidence, not a serial ACK alone.

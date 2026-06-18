# Hardware Validation

No physical HIL validation was performed while creating this document. A dry
run or generated command plan is not hardware evidence. I2C ACK proves address
response only; it does not prove PCA9555 chip identity because the PCA9555 has
no documented chip ID register.

Use this file for both planning and recording PCA9555 bench validation. Keep
generated run artifacts under `hil_logs/`, which is intentionally ignored by
git.

## Bench Setup

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
| Evidence directory | `hil_logs/i2c_<timestamp>/` |

Safety rules:

- Use current-limited loads before any output-driving test.
- Do not externally drive a PCA9555 pin while it is configured as an output.
- Tie A0/A1/A2, INT, and unused inputs deliberately.
- Switch only the intended rail during brownout tests and keep signals through
  safe impedance.
- `probe()` proves address response only. It is not chip identity.
- `recover()` reapplies cached desired state. It cannot force a true PCA9555
  power-on reset.

## Build And Run

Build Arduino examples:

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

## Default HIL Command Sequence

The default runner sequence is read-oriented and avoids output-driving commands.

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

## Opt-In Checks

These checks must not be treated as serial-only PASS results:

| Check | Default handling | Reason |
| --- | --- | --- |
| `selftest` | `SKIPPED_UNSAFE` unless `--include-output-tests` | Mutates output latches, direction, and polarity before restoring |
| `stress_mix 100` | `SKIPPED_UNSAFE` unless `--include-output-tests` | Runs mixed read/write/config/polarity operations |
| `stress 1000` | `SKIPPED_UNSAFE` unless `--include-soak` | Longer soak gate |
| Output patterns, `allhigh`, `alllow`, `walk`, `sweep` | Manual/operator only | Drives physical pins and attached loads |
| Fault injection, disconnects, brownout | Manual/operator only | Requires safe handling and bench controls |
| INT behavior and errata pointer park | OPERATOR_CHECK_REQUIRED | Requires wiring, logic analyzer, or MCU capture |

For opt-in mutating commands that declare a recovery action, the runner inserts
the recovery command into the execution plan. The current restore-safe-state
command is `dirin 0xFFFF`.

Even if all serial commands classify as PASS, the runner's final verdict remains
`OPERATOR_REVIEW_REQUIRED` until manual wiring, physical output, INT, errata,
and fault/recovery evidence is reviewed.

## Evidence Checklist

| Item | Status | Evidence path |
| --- | --- | --- |
| `serial_transcript.txt` captured | NOT RUN | |
| `summary.md` captured | NOT RUN | |
| `summary.json` captured | NOT RUN | |
| `operator_checklist.md` completed | NOT RUN | |
| Wiring photo attached | NOT RUN | |
| Address strap table attached | NOT RUN | |
| Safe load/current limit documented | NOT RUN | |
| Per-pin input observation recorded | OPERATOR_CHECK_REQUIRED | |
| Physical output observation recorded | OPERATOR_CHECK_REQUIRED | |
| INT assertion/clear capture attached | OPERATOR_CHECK_REQUIRED | |
| Errata pointer-park I2C decode attached | OPERATOR_CHECK_REQUIRED | |
| Fault injection evidence attached | OPERATOR_CHECK_REQUIRED | |

## Validation Matrix

| Test | Setup | Procedure | Expected result | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| Address scan/probe | A0/A1/A2 strapped for each intended `0x20`-`0x27` address | Run `scan`, `probe`, and `drv` for each wired address | Only intended addresses respond; `probe` returns OK without updating health counters | NOT RUN | |
| POR defaults | Full PCA9555 power cycle; fixture input states documented | Run `dump`, `outputs`, `config`, `polarity`, and `inputs` before mutation | Config `0xFFFF`; output latch `0xFFFF`; polarity `0x0000`; input values match physical levels | NOT RUN | |
| Input reads on all pins | All pins input; each pin can be pulled low safely | Run `dirin 0xFFFF`; read released/high and pulled-low state for each pin | Linear pins `0-15` map to `P00-P07` / `P10-P17`; high reads `1`, low reads `0` before polarity inversion | NOT RUN | |
| Output writes on all pins | Safe current-limited loads; no external driver conflicts | Use safe-output APIs or CLI equivalents, then `alllow`, `allhigh`, `pattern`, `walk`, and `sweep` | Physical outputs match latches with no excessive current, resets, or I2C errors | NOT RUN | |
| Bulk mask writes | Known latch baseline and observable safe loads | Apply masked output changes and read back `outputs` | Selected bits change; unrelated latch bits remain unchanged | NOT RUN | |
| Latch preload ordering | Logic analyzer on SDA/SCL and selected pins | Capture input-to-output transition via `configureOutputs()` or `preloadOutput()` plus `setDirection()` | I2C decode shows Output Port write before Configuration write; selected pin does not glitch opposite requested preload | NOT RUN | |
| Output-to-input interrupt behavior | INT pulled up and captured | Drive known output, change to input, observe INT, then read input port | False interrupt behavior is observed/handled and input read rebaselines the port | NOT RUN | |
| Polarity inversion | Known high/low input levels | Toggle polarity, then read input and output latch | Input sense inverts when polarity bit is set; output latch readback is unchanged | NOT RUN | |
| INT port 0 clear | INT pulled up; Port 0 input switchable | Baseline inputs, toggle Port 0 input, read Port 0 | INT asserts on change and clears after reading Port 0 | NOT RUN | |
| INT port 1 clear | INT pulled up; Port 1 input switchable | Baseline inputs, toggle Port 1 input, read Port 1 | INT asserts on change and clears after reading Port 1 | NOT RUN | |
| Both-port INT clear | One input on each port switchable | Toggle both, call `readInputsAndClearInterrupt()` or `clearInterrupts()` | INT clears after both input ports are read | NOT RUN | |
| INT service edge policy | INT pulled up; one switchable input; firmware can re-read/debounce | Toggle near service timing or simulate repeated changes while servicing INT | Application re-read/debounce policy prevents missed or ambiguous input state | NOT RUN | |
| Errata pointer park | PCA9555 plus a second readable I2C slave; I2C analyzer | Run input reads, then other-slave reads through serialized bus manager | Pointer parks at command `0x02`; no unrelated transaction interleaves in synchronous path | NOT RUN | |
| Wrong address/NACK | Unused address or safe disconnect | Run `probe`, `begin`, `drv`, and selected calls | Errors map to documented `Status`; health/offline behavior matches contract | NOT RUN | |
| Unplug/replug recovery | PCA9555 can be safely disconnected/reconnected | Reach OFFLINE, reconnect, run `recover`, then `drv` and inputs | Normal I/O is blocked while offline; `recover()` returns READY after reconnect | NOT RUN | |
| Brownout/power-cycle recovery | PCA9555 VCC independently switchable | Drive known state, power-cycle PCA9555 only, run `recover` | Hardware may show POR defaults before recovery; cached state reapplies after recovery | NOT RUN | |
| 100 kHz operation | Firmware/app sets I2C to 100 kHz | Run scan/probe/selftest/stress and selected I/O checks | Operations complete without unexplained failures; final health READY | NOT RUN | |
| 400 kHz operation | Repo default or equivalent 400 kHz setup | Run scan/probe/selftest/stress and selected I/O checks | Operations complete without unexplained failures; final health READY | NOT RUN | |
| Shared-bus soak | PCA9555 plus another active I2C target | Run long read/write soak with other-target traffic through bus manager | No hangs, resets, unexpected INT loss, or unexplained I2C failures | NOT RUN | |
| Arduino ESP32-S2 run | `env:esp32s2dev` and CLI example | Upload, monitor, run version/help, scan/probe, read inputs, safe output toggle, and `drv` | CLI runs and final health is READY | NOT RUN | |
| Arduino ESP32-S3 run | `env:esp32s3dev` and CLI example | Upload, monitor, run version/help, scan/probe, read inputs, safe output toggle, and `drv` | CLI runs and final health is READY | NOT RUN | |
| ESP-IDF hardware run | Native `examples/espidf_basic` or equivalent app | Build/flash native IDF app and run basic input/output/recovery checks | Native IDF path works without Arduino/Wire; final cleanup succeeds | NOT RUN | |

## Claim Summary

This document supports only rows with attached evidence. It must not be used to
claim production-ready, industry-grade, field-proven, or hardware validated
status unless the release checklist and hardware matrix are fully completed and
reviewed.

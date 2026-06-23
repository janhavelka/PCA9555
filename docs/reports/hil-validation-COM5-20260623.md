# PCA9555 HIL Validation And Repository Audit - COM5 - 2026-06-23

## Summary

Formal release verdict: **not a passing 8-hour HIL sign-off yet**.

The ESP32-S3 on `COM5` completed the requested 8-hour read-only soak window and the device-side PCA9555 health stayed `READY` with zero driver-recorded I2C failures. The original host runner nevertheless produced a `FAIL` verdict because its serial completion logic allowed idle output gaps to complete commands before the CLI prompt, causing partial host-side captures: 6 command timeouts and 85 review-classified partial results.

The runner completion defect was fixed after the 8-hour run. A 60-second prompt-gated regression on the same hardware passed with 272/272 soak commands, zero failures, and final device health `READY`. A new 8-hour run with the fixed runner is still required before claiming final HIL pass.

## Setup

| Item | Value |
| --- | --- |
| Host OS | Windows 11 Education 64-bit, 10.0.26200 |
| PlatformIO | Core 6.1.18, with warning about multiple/obsolete PIO cores |
| Hardware port | `COM5` |
| COM5 identity | USB VID:PID `303A:1001`, serial `24:58:7C:DB:DB:AC` |
| Target identified by upload | ESP32-S3 QFN56 rev v0.1, USB-Serial/JTAG, MAC `24:58:7c:db:db:ac` |
| Firmware env | `esp32s3dev` |
| Baud | `115200` |
| I2C address under test | `0x20` |
| I2C devices seen by scan | `0x20`, `0x40`, `0x41`, `0x50` |
| Branch | `main` |
| Base commit at HIL start | `ba2b2011740750be4072ce03705963f4c662f419` |
| Initial worktree | Clean before runner/documentation edits |
| Firmware version banner | `1.1.0 (ba2b201, 2026-06-22 20:26:46, dirty)` |

The firmware was dirty during HIL because runner/documentation changes were already present. The driver fixes listed below were made after HIL evidence collection and verified with native tests and builds; they were not part of the flashed 8-hour firmware image.

## Artifacts

| Run | Path |
| --- | --- |
| Functional HIL | `hil_logs/i2c_20260622_202718/summary.md` |
| Short benchmark | `hil_logs/i2c_20260622_202808/summary.md` |
| 8-hour soak | `hil_logs/soak_20260622_203000/i2c_20260622_202836/summary.md` |
| 8-hour machine summary | `hil_logs/soak_20260622_203000/i2c_20260622_202836/summary.json` |
| 8-hour serial transcript | `hil_logs/soak_20260622_203000/i2c_20260622_202836/serial_transcript.txt` |
| 8-hour soak transcript | `hil_logs/soak_20260622_203000/i2c_20260622_202836/soak_transcript.txt` |
| Post-fix 60-second regression | `hil_logs/i2c_20260623_043252/summary.md` |
| Runner summary copy | `docs/reports/hil-validation-COM5-20260622-runner-summary.md` |

## HIL Commands

Functional pass:

```powershell
python tools\run_i2c_hil.py --port COM5 --baud 115200 --address 0x20 --timeout-s 8 --idle-timeout-s 0.5 --boot-settle-s 2 --startup-timeout 20 --verbose
```

8-hour soak:

```powershell
python tools\run_i2c_hil.py --port COM5 --baud 115200 --address 0x20 --timeout-s 8 --idle-timeout-s 0.5 --startup-timeout 20 --benchmark-command read --benchmark-count 50 --benchmark-warmup 3 --soak-duration-s 28800 --soak-command-mix read,outputs,config,polarity,health,probe --soak-interval-s 0.2 --soak-failure-limit 3 --out hil_logs\soak_20260622_203000 --report docs\reports\hil-validation-COM5-20260622-runner-summary.md
```

Prompt-gated regression after runner fix:

```powershell
python tools\run_i2c_hil.py --port COM5 --baud 115200 --address 0x20 --timeout-s 8 --idle-timeout-s 1.0 --startup-timeout 20 --soak-duration-s 60 --soak-command-mix read,outputs,config,polarity,health,probe --soak-interval-s 0.2 --soak-failure-limit 3 --benchmark-command read --benchmark-count 20 --benchmark-warmup 2 --verbose
```

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Build `esp32s3dev` before HIL | PASS | PlatformIO build completed |
| Upload to COM5 | PASS | Flash/write verified by esptool |
| Functional serial HIL | OPERATOR_REVIEW_REQUIRED | All read-only serial commands passed; manual electrical checks not performed |
| I2C scan | PASS | ACKs at `0x20`, `0x40`, `0x41`, `0x50`; scan is not chip identity proof |
| Probe at `0x20` | PASS | Raw driver probe OK; ACK-only |
| 50-command read benchmark | PASS | 50/50 pass, mean 0.02094 s, max 0.032 s, 47.755 Hz |
| 8-hour soak | FAIL | 129,712 commands, 129,621 PASS, 6 TIMEOUT, 85 SERIAL_OK_OR_REVIEW |
| 8-hour device health | PASS | Final health `READY`, total success 108,236, total failures 0, last error never |
| Post-fix 60-second soak | PASS | 272/272 pass, zero failures, mean 0.02041 s, max 0.032 s |

## 8-Hour Soak Detail

| Metric | Value |
| --- | ---: |
| Started | `2026-06-22T20:28:38` |
| Ended | `2026-06-23T04:28:38` |
| Duration | 28,800 s |
| Completed commands | 129,712 |
| Failures counted by runner | 6 |
| PASS results | 129,621 |
| TIMEOUT results | 6 |
| Review results | 85 |
| Minimum latency | 0.000 s |
| Mean latency | 0.02119 s |
| Maximum latency | 8.016 s |
| Effective command rate | 4.504 Hz |
| Stop reason | `duration_limit` |

Command counts:

| Command | Count |
| --- | ---: |
| `read` | 21,619 |
| `outputs` | 21,619 |
| `config` | 21,619 |
| `polarity` | 21,619 |
| `health` | 21,618 |
| `probe` | 21,618 |

The final transcript tail showed the device still responding normally:

- Inputs: `0x0000`
- Output latches: `0xFFFF`
- Configuration: `0xFFFF`
- Polarity: `0x0000`
- Driver health: `READY`, zero total failures, last error never

Interpretation: the hardware and driver stayed stable for the duration, but the host evidence is not clean enough for a formal pass because the runner was allowed to classify incomplete serial captures.

## Runner Fixes Implemented

- Added `--timeout-s` and `--idle-timeout-s` aliases.
- Added boot-settle and bounded serial reconnect options.
- Added parser self-test mode.
- Added bounded benchmark and soak modes with aggregate JSON/Markdown output.
- Added classifier fields to command results.
- Added `--report` copy support.
- Changed command completion to require the CLI prompt or explicit completion token by default.
- Added `--allow-idle-completion` as an explicit opt-in for promptless command surfaces.
- Added artifact tests for parser/report fields and the prompt-gated default.
- Updated `docs/hardware_validation.md` and `README.md` with the new runner flow.

## Driver Fixes Implemented After HIL

- `applyInterruptErrataWorkaround()` now checks normal operation state before acquiring optional lock hooks, so an OFFLINE driver returns immediately without lock or I2C side effects.
- Cache-derived no-op mutators now return `BUSY` with `"Hardware state dirty; call recover()"` while `hardwareStateDirty()` is set, instead of reporting fake success from possibly stale cache.
- Chunked no-op job starts use the same dirty-state guard.
- `getLastReadInputs()` now reports `NOT_INITIALIZED` before `begin()` and `BUSY` until an explicit full input-pair read has completed.

Native regression coverage was added for each driver fix.

## Remaining Limitations

- A new 8-hour soak with the fixed prompt-gated runner is required for release-grade HIL sign-off.
- Output-driving and mixed stress commands were intentionally not run because the available fixture state was not proven safe for mutating PCA9555 latches/directions/polarity.
- PCA9555 has no chip ID register; scan/probe evidence proves address acknowledgement, not silicon identity.
- Example-only Arduino transport still treats per-call timeout as advisory because timeout ownership stays with `initWire()` and the shared `Wire` bus manager.
- Example-only scanner temporarily changes `Wire` timeout during scans; this remains acceptable for diagnostics but should not be used as a production bus manager.

## Verification After Fixes

The following checks were run after the driver and runner fixes:

```powershell
python tools\test_run_i2c_hil_parser.py
python tools\check_hil_contract.py
python tools\check_cli_contract.py
python tools\check_core_timing_guard.py
python tools\check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
```

At the time this report was written, native tests passed with 154/154 test cases. The full final verification pass is recorded in the task response.

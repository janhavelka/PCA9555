# PCA9555 COM5 Production-Candidate Validation

Status label: `BLOCKED_HIL_FAILURE`

Date: 2026-06-23

## Scope

This pass attempted to regenerate release evidence for the PCA9555 library using
the current dirty worktree and the COM5 ESP32-S3 fixture. The run does not
support production-grade, field-validated, or fully hardware-validated wording.

The blocking gate is the required fresh 8-hour HIL soak. Multiple prompt-gated
runs produced one serial `TIMEOUT` before the 28800 second duration completed.

## Repository And Host Evidence

| Field | Value |
| --- | --- |
| Branch | `main` |
| Commit | `dcffeba87e30d9a7d7f3f3ceeb54766032af3a96` |
| Worktree | Dirty |
| Host OS | Microsoft Windows 11 Education 10.0.26200 64-bit |
| Python | 3.12.10 |
| PlatformIO | 6.1.18 |
| Serial port | `COM5` |
| Baud | 115200 |
| I2C address | `0x20` |
| Firmware banner | `PCA9555 library version: 1.1.0` |

Dirty files observed during the final evidence runs:

```text
M CHANGELOG.md
M docs/reports/hil-validation-COM5-20260623-runner-summary.md
M examples/common/HealthDiag.h
M tools/run_i2c_hil.py
M tools/test_run_i2c_hil_parser.py
?? docs/reports/hil-validation-COM5-20260623-runner-summary-failed-133529.md
?? docs/reports/hil-validation-COM5-20260623-runner-summary-failed-161401.md
?? docs/reports/hil-validation-COM5-20260623-runner-summary-failed-181735.md
?? docs/reports/hil-validation-COM5-20260623-runner-summary-failed-202601.md
?? docs/reports/hil-validation-COM5-20260623-production-candidate.md
?? docs/reports/release-readiness-20260623.md
```

The firmware image used for COM5 was built from the same commit plus dirty
example-code changes in `examples/common/HealthDiag.h`. Host-only runner changes
in `tools/` affected evidence collection, not the firmware image.

## Static, Native, And Build Gates

| Gate | Result |
| --- | --- |
| `python tools\test_run_i2c_hil_parser.py` | PASS |
| `python tools\check_hil_contract.py` | PASS |
| `python tools\check_cli_contract.py` | PASS |
| `python tools\check_core_timing_guard.py` | PASS |
| `python tools\check_idf_example_contract.py` | PASS |
| `python scripts\generate_version.py check` | PASS |
| `python -m py_compile tools\run_i2c_hil.py` | PASS |
| `git diff --check` | PASS; line-ending warnings only |
| `python -m platformio test -e native` | PASS, 158/158 |
| `python -m platformio run -e esp32s2dev` | PASS |
| `python -m platformio run -e esp32s3dev` | PASS |
| `python -m platformio pkg pack` | PASS; generated `PCA9555-1.1.0.tar.gz` removed |
| `idf.py --version` | BLOCKED; `idf.py` not found in this shell |

## HIL Evidence

All HIL runs used prompt-gated command completion. `--allow-idle-completion` was
not used.

| Run | Command class | Result | Evidence |
| --- | --- | --- | --- |
| 40-minute reconnect diagnostic | `read,outputs,config,polarity,health,probe`, 2400 s, serial reopen 900 s | Serial PASS, final `OPERATOR_REVIEW_REQUIRED` due manual rows | `hil_logs\diagnostic_reopen_20260623\i2c_20260623_173510` |
| 8-hour production attempt | 28800 s, serial reopen 1800 s | FAIL after 6126.813 s: 30264 PASS, 1 `TIMEOUT`, 3 reopens | `hil_logs\production_candidate_20260623\i2c_20260623_181735` |
| Post-failure diagnostic | 120 s | Serial PASS, final `OPERATOR_REVIEW_REQUIRED` due manual rows | `hil_logs\post_reopen_fail_diag_20260623\i2c_20260623_202330` |
| 8-hour production attempt | 28800 s, serial reopen 600 s | FAIL after 3354.890 s: 16521 PASS, 1 `TIMEOUT`, 5 reopens | `hil_logs\production_candidate_20260623\i2c_20260623_202601` |
| Post-failure diagnostic | 60 s | Serial PASS, final `OPERATOR_REVIEW_REQUIRED` due manual rows | `hil_logs\post_600_fail_diag_20260623\i2c_20260623_220236` |

Preserved failed runner reports:

- `docs\reports\hil-validation-COM5-20260623-runner-summary-failed-133529.md`
- `docs\reports\hil-validation-COM5-20260623-runner-summary-failed-161401.md`
- `docs\reports\hil-validation-COM5-20260623-runner-summary-failed-181735.md`
- `docs\reports\hil-validation-COM5-20260623-runner-summary-failed-202601.md`

Current runner summary:

- `docs\reports\hil-validation-COM5-20260623-runner-summary.md`

## HIL Failure Classification

The two final 8-hour attempts failed with one empty serial response to a normal
soak command:

- `i2c_20260623_181735`: timeout on `soak:read`.
- `i2c_20260623_202601`: timeout on `soak:polarity`.

In both cases, immediately preceding health snapshots reported:

```text
State: READY Online: yes Consecutive failures: 0 Total failures: 0 Last error: never
```

Short post-failure diagnostics passed immediately after reopening COM5. This
suggests the PCA9555 driver and I2C device were not left failed, but the
production HIL gate still failed because the host runner observed a command
`TIMEOUT`. No rerun may be used to hide this without a concrete root-cause fix.

Serial reopen was tested only as a host-session mitigation. On this USB CDC
target, reopening the COM port resets or reinitializes firmware health counters,
so reconnect-mediated runs must not be used as proof of uninterrupted firmware
uptime.

## Hardware Rows

Completed without physical attention:

- Arduino ESP32-S3 serial CLI read-oriented commands on COM5.
- Short post-failure read-oriented checks.

Still `NOT RUN` or operator-required:

- Safe output fixture documentation and output-driving HIL.
- INT assertion/clear evidence.
- Errata pointer-park logic-analyzer evidence.
- Fault injection and manual recovery evidence.
- 100 kHz and 400 kHz validation windows.
- Shared-bus validation.
- Native ESP-IDF build and hardware smoke.
- Wiring photo, pin/load table, pull-up documentation, and chip/module identity evidence.

## Decision

Production validation did not pass. Release/version tagging is deferred.

If these changes are accepted later, the source and tooling fixes are compatible
with a patch release, but the current evidence does not justify a production
claim or release-candidate sign-off.

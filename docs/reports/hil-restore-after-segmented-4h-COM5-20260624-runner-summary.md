# PCA9555 I2C HIL Summary

- Date/time: 2026-06-24T18:27:46
- Branch: `main`
- Commit: `dcffeba87e30d9a7d7f3f3ceeb54766032af3a96`
- Worktree: `M CHANGELOG.md
 M docs/reports/hil-validation-COM5-20260623-runner-summary.md
 M examples/common/HealthDiag.h
 M tools/run_i2c_hil.py
 M tools/test_run_i2c_hil_parser.py
?? docs/reports/hil-full-function-COM5-20260624-commands.txt
?? docs/reports/hil-full-function-COM5-20260624-runner-summary-failed-125345.md
?? docs/reports/hil-full-function-COM5-20260624-runner-summary-review-130300.md
?? docs/reports/hil-full-function-COM5-20260624-runner-summary-timeout-130746.md
?? docs/reports/hil-full-function-COM5-20260624-runner-summary-timeout-131541.md
?? docs/reports/hil-full-function-COM5-20260624-runner-summary.md
?? docs/reports/hil-full-function-COM5-20260624-smoke2-summary.md
?? docs/reports/hil-restore-COM5-20260624-runner-summary.md
?? docs/reports/hil-restore-after-slow-timeout-COM5-20260624-runner-summary.md
?? docs/reports/hil-restore-after-timeout-COM5-20260624-runner-summary.md
?? docs/reports/hil-validation-COM5-20260623-production-candidate.md
?? docs/reports/hil-validation-COM5-20260623-runner-summary-failed-133529.md
?? docs/reports/hil-validation-COM5-20260623-runner-summary-failed-161401.md
?? docs/reports/hil-validation-COM5-20260623-runner-summary-failed-181735.md
?? docs/reports/hil-validation-COM5-20260623-runner-summary-failed-202601.md
?? docs/reports/release-readiness-20260623.md`
- Serial port: `COM5`
- Baud: `115200`
- Serial DTR: `0`
- Serial RTS: `0`
- I2C address: `0x20`
- Timeout override: `8.0`
- Startup timeout: `20.0`
- Idle timeout: `1.0`
- Allow idle completion: `False`
- Boot settle: `2.0`
- Serial reopen interval: `0.0`
- Serial reopen note: host serial is closed and reopened only between aggregate commands; USB CDC targets may reset, so this is not proof of uninterrupted firmware uptime.
- Dry run: `False`
- Final verdict: `OPERATOR_REVIEW_REQUIRED`

## Command Sequence

| Command | Classifier | Purpose | Serial Result | Operator Result | Completion | Elapsed | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `version` | `section` | Print firmware and library version information. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `help` | `section` | Capture CLI command surface in the transcript. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `scan` | `scan` | Scan I2C addresses; ACK proves address response only. | `PASS` | `N/A` | `prompt` | 0.14s | A scan proves only that an address acknowledged. PCA9555 has no documented chip ID register. |
| `probe` | `probe` | Run the driver raw address probe without health tracking. | `PASS` | `N/A` | `prompt` | 0.00s | Probe is ACK-only and does not prove chip identity. |
| `settings` | `section` | Capture active driver settings. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `read` | `read` | Read both input ports; clears PCA9555 input interrupt state. | `PASS` | `N/A` | `prompt` | 0.00s | Input reads clear interrupt sources and apply the errata pointer-park write. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `config` | `read` | Read configuration registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `polarity` | `read` | Read polarity inversion registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `dump` | `read` | Capture all PCA9555 register pairs exposed by the CLI. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `pins` | `read` | Capture per-pin input, output latch, direction, and polarity summary. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `health` | `health` | Capture driver health before stress. | `PASS` | `N/A` | `prompt` | 0.00s | Historical last-error fields may be nonzero; current READY state is the key gate. |
| `stress 10` | `stress` | Run bounded read-only input stress using the CLI async stress command. | `PASS` | `N/A` | `prompt` | 0.02s | This repeatedly reads inputs and clears PCA9555 interrupt state. |
| `health` | `health` | Capture final driver health after stress. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `selftest confirm` | `selftest` | Run CLI API self-test that mutates output, direction, and polarity state. | `PASS` | `N/A` | `prompt` | 0.02s | The CLI labels this safe, but it changes PCA9555 latches, direction, and polarity before restoring. Use only on a known-safe fixture. |
| `dirin 0xFFFF confirm` | `direction` | Restore-safe-state command after `selftest confirm`. | `PASS` | `N/A` | `prompt` | 0.00s | Automatic recovery command from HIL metadata. Restores all pins to input; output-to-input changes can trigger PCA9555 INT behavior. |
| `stress 1000` | `stress` | Longer read-only stress soak. | `PASS` | `N/A` | `prompt` | 0.31s | Longer soak is optional and still clears input interrupt state. |
| `stress_mix 100 confirm` | `stress_mix` | Mixed read/write/config/polarity/mask stress test. | `PASS` | `N/A` | `prompt` | 0.02s | Mixed stress drives outputs and changes configuration; opt-in only. |
| `dirin 0xFFFF confirm` | `direction` | Restore-safe-state command after `stress_mix 100 confirm`. | `PASS` | `N/A` | `prompt` | 0.00s | Automatic recovery command from HIL metadata. Restores all pins to input; output-to-input changes can trigger PCA9555 INT behavior. |
| `soak:aggregate` | `soak` | Aggregate statistics for soak. | `PASS` | `N/A` | `command_limit` | 0.11s | Generated by bounded repeated-command runner. |

## Aggregate Timing

| Label | Completed | Failures | Serial Reopens | Min | Mean | Max | Effective Hz | Stop Reason |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `soak` | 2 | 0 | 0 | 0.000s | 0.000s | 0.000s | 18.349 | `command_limit` |

## Evidence Excerpts

## Artifacts

- Serial transcript: `hil_logs\restore_after_segmented_4h_20260624\i2c_20260624_182743\serial_transcript.txt`
- Operator checklist: `hil_logs\restore_after_segmented_4h_20260624\i2c_20260624_182743\operator_checklist.md`
- Machine summary: `hil_logs\restore_after_segmented_4h_20260624\i2c_20260624_182743\summary.json`

## Identity And Hardware Claim Guard

No physical HIL validation is implied by a dry run. A scan or probe proves only I2C ACK at the address, not PCA9555 identity.

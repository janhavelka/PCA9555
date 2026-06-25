# PCA9555 I2C HIL Summary

- Date/time: 2026-06-24T13:13:40
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
?? docs/reports/hil-full-function-COM5-20260624-runner-summary.md
?? docs/reports/hil-full-function-COM5-20260624-smoke2-summary.md
?? docs/reports/hil-restore-COM5-20260624-runner-summary.md
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
- Final verdict: `FAIL`

## Command Sequence

| Command | Classifier | Purpose | Serial Result | Operator Result | Completion | Elapsed | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `version` | `section` | Print firmware and library version information. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `help` | `section` | Capture CLI command surface in the transcript. | `PASS` | `N/A` | `prompt` | 0.02s |  |
| `scan` | `scan` | Scan I2C addresses; ACK proves address response only. | `PASS` | `N/A` | `prompt` | 0.14s | A scan proves only that an address acknowledged. PCA9555 has no documented chip ID register. |
| `probe` | `probe` | Run the driver raw address probe without health tracking. | `PASS` | `N/A` | `prompt` | 0.00s | Probe is ACK-only and does not prove chip identity. |
| `settings` | `section` | Capture active driver settings. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `health` | `health` | Capture driver health before stress. | `PASS` | `N/A` | `prompt` | 0.00s | Historical last-error fields may be nonzero; current READY state is the key gate. |
| `read` | `read` | Read both input ports; clears PCA9555 input interrupt state. | `PASS` | `N/A` | `prompt` | 0.00s | Input reads clear interrupt sources and apply the errata pointer-park write. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `config` | `read` | Read configuration registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `polarity` | `read` | Read polarity inversion registers. | `PASS` | `N/A` | `prompt` | 0.02s |  |
| `dump` | `read` | Capture all PCA9555 register pairs exposed by the CLI. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `pins` | `read` | Capture per-pin input, output latch, direction, and polarity summary. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `allhigh confirm` | `output_pattern` | Drive all PCA9555 pins high as outputs. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `alllow confirm` | `output_pattern` | Drive all PCA9555 pins low as outputs. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.02s |  |
| `pattern 0xAAAA confirm` | `output_pattern` | Drive an exact 16-bit PCA9555 output pattern. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `pattern 0x5555 confirm` | `output_pattern` | Drive an exact 16-bit PCA9555 output pattern. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `walk 0 confirm` | `output_pattern` | Run walking-1 output pattern across all 16 pins. | `PASS` | `N/A` | `prompt` | 0.02s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `sweep 0 confirm` | `output_pattern` | Run accumulating output ON/OFF sweep across all 16 pins. | `PASS` | `N/A` | `prompt` | 0.02s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `setbits 0x00FF confirm` | `mask_write` | Set masked output latch bits high. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `clearbits 0x0F0F confirm` | `mask_write` | Clear masked output latch bits low. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `togglebits 0x3333 confirm` | `mask_write` | Toggle masked output latch bits. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `wpin 0 1 confirm` | `pin_write` | Write one output latch bit. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.02s |  |
| `toggle 0 confirm` | `pin_write` | Toggle one output latch bit. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `wport 0 0xA5 confirm` | `port_write` | Write one output port latch register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `wport 1 0x5A confirm` | `port_write` | Write one output port latch register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `dirout 0x00FF confirm` | `direction` | Configure masked pins as outputs. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `config` | `read` | Read configuration registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `dirin 0x0F0F confirm` | `direction` | Configure masked pins as inputs. | `PASS` | `N/A` | `prompt` | 0.02s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `config` | `read` | Read configuration registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `dir 15 out confirm` | `direction` | Set one pin direction. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `config` | `read` | Read configuration registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `dport 0 0x00 confirm` | `direction` | Set one port direction register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `dport 1 0xFF confirm` | `direction` | Set one port direction register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `config` | `read` | Read configuration registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `invertset 0x00FF confirm` | `polarity` | Enable masked input polarity inversion. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `polarity` | `read` | Read polarity inversion registers. | `PASS` | `N/A` | `prompt` | 0.02s |  |
| `invertclr 0x0F0F confirm` | `polarity` | Disable masked input polarity inversion. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `polarity` | `read` | Read polarity inversion registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `pol 15 1 confirm` | `polarity` | Set one pin input polarity inversion bit. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `polarity` | `read` | Read polarity inversion registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `wpol 0 0xAA confirm` | `polarity` | Set one port polarity inversion register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `wpol 1 0x55 confirm` | `polarity` | Set one port polarity inversion register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `polarity` | `read` | Read polarity inversion registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `rreg 0` | `register_read` | Read one PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.01s | Dynamic CLI command. |
| `rreg 1` | `register_read` | Read one PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command. |
| `rregs 2 2` | `register_read` | Read one or two PCA9555 registers with pair wrapping. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command. |
| `wreg 2 0xCC confirm` | `register_write` | Write one writable PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `rreg 2` | `register_read` | Read one PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command. |
| `wreg 3 0x33 confirm` | `register_write` | Write one writable PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `outputs` | `read` | Read output latch registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `wreg 4 0x00 confirm` | `register_write` | Write one writable PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `wreg 5 0xFF confirm` | `register_write` | Write one writable PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.02s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `config` | `read` | Read configuration registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `wreg 6 0x0F confirm` | `register_write` | Write one writable PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `wreg 7 0xF0 confirm` | `register_write` | Write one writable PCA9555 register. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `polarity` | `read` | Read polarity inversion registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `wregs 2 0xAA 0x55 confirm` | `register_write` | Write one or two writable PCA9555 registers with pair wrapping. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `rregs 2 2` | `register_read` | Read one or two PCA9555 registers with pair wrapping. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command. |
| `wregs 4 0xFF 0xFF confirm` | `register_write` | Write one or two writable PCA9555 registers with pair wrapping. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `config` | `read` | Read configuration registers. | `PASS` | `N/A` | `prompt` | 0.02s |  |
| `wregs 6 0x00 0x00 confirm` | `register_write` | Write one or two writable PCA9555 registers with pair wrapping. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `polarity` | `read` | Read polarity inversion registers. | `PASS` | `N/A` | `prompt` | 0.00s |  |
| `selftest confirm` | `selftest` | Run CLI API self-test that mutates output, direction, and polarity state. | `PASS` | `N/A` | `prompt` | 0.02s | The CLI labels this safe, but it changes PCA9555 latches, direction, and polarity before restoring. Use only on a known-safe fixture. |
| `dirin 0xFFFF confirm` | `direction` | Restore-safe-state command after `selftest confirm`. | `PASS` | `N/A` | `prompt` | 0.00s | Automatic recovery command from HIL metadata. Restores all pins to input; output-to-input changes can trigger PCA9555 INT behavior. |
| `stress_mix 100 confirm` | `stress_mix` | Mixed read/write/config/polarity/mask stress test. | `PASS` | `N/A` | `prompt` | 0.03s | Mixed stress drives outputs and changes configuration; opt-in only. |
| `dirin 0xFFFF confirm` | `direction` | Restore-safe-state command after `stress_mix 100 confirm`. | `PASS` | `N/A` | `prompt` | 0.00s | Automatic recovery command from HIL metadata. Restores all pins to input; output-to-input changes can trigger PCA9555 INT behavior. |
| `recover confirm` | `recovery` | Run manual driver recovery and reapply cached state. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `dirin 0xFFFF confirm` | `direction` | Configure masked pins as inputs. | `PASS` | `N/A` | `prompt` | 0.00s | Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in. |
| `health` | `health` | Capture driver health before stress. | `PASS` | `N/A` | `prompt` | 0.00s | Historical last-error fields may be nonzero; current READY state is the key gate. |
| `soak:aggregate` | `soak` | Aggregate statistics for soak. | `FAIL` | `N/A` | `failure_limit` | 350.83s | Generated by bounded repeated-command runner. |

## Aggregate Timing

| Label | Completed | Failures | Serial Reopens | Min | Mean | Max | Effective Hz | Stop Reason |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `soak` | 3317 | 1 | 0 | 0.000s | 0.005s | 8.031s | 9.455 | `failure_limit` |

## Evidence Excerpts

### `soak:aggregate`
- `completed=3317`
- `failures=1`
- `result_counts={"PASS": 3316, "TIMEOUT": 1}`
- `effective_hz=9.455`
- `stop_reason=failure_limit`

## Artifacts

- Serial transcript: `hil_logs\full_function_4h_20260624_final\i2c_20260624_130746\serial_transcript.txt`
- Operator checklist: `hil_logs\full_function_4h_20260624_final\i2c_20260624_130746\operator_checklist.md`
- Machine summary: `hil_logs\full_function_4h_20260624_final\i2c_20260624_130746\summary.json`

## Identity And Hardware Claim Guard

No physical HIL validation is implied by a dry run. A scan or probe proves only I2C ACK at the address, not PCA9555 identity.

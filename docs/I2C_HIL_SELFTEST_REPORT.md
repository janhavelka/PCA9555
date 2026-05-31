# PCA9555 I2C HIL Self-Test Report

Date: 2026-05-31
Branch: `hardening/pca9555-industry-readiness`
Base commit hash before HIL changes: `9c9f66f3d2d5cf3647b5fc2232603234af3bae27`
New branch created: no

## Scope

This report covers the host-side Python HIL runner and auditor-facing
documentation added for the existing PCA9555 serial CLI. No physical HIL
validation was performed. A scan or probe proves I2C ACK only; ACK is not
PCA9555 chip identity because the device has no documented identity register.

## Files Changed

- `.gitignore` adds `hil_logs/`
- `.github/workflows/ci.yml` runs the HIL contract guard in CI
- `README.md` links the HIL docs and local dry-run command
- `docs/PCA9555_HARDWARE_VALIDATION_MATRIX.md` records the HIL runner command
- `tools/run_i2c_hil.py`
- `tools/check_hil_contract.py`
- `docs/I2C_HIL_RUNBOOK.md`
- `docs/I2C_HIL_TARGET_TEMPLATE.md`
- `docs/I2C_HIL_SELFTEST_REPORT.md`

## Command Sequence

The runner default sequence is:

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

The exact operator command for a real HIL run is:

```bash
python tools/run_i2c_hil.py --port <PORT> --baud 115200 --address 0x20
```

Dry-run command:

```bash
python tools/run_i2c_hil.py --dry-run
```

Install serial support with:

```bash
python -m pip install pyserial
```

## Safety Exclusions

The default sequence avoids output-driving GPIO commands. `selftest`,
`stress_mix`, output patterns, `allhigh`, `alllow`, `walk`, `sweep`, direct
write commands, fault injection, and brownout/disconnect tests are opt-in or
manual-only. They remain `SKIPPED_UNSAFE` or `OPERATOR_CHECK_REQUIRED` until the
operator confirms a safe fixture and records evidence.

Custom command files are also checked against the known mutating CLI command
surface. Unsafe custom commands are marked destructive and require
`--include-output-tests`.

The runner does not emit final verdict `PASS` while manual HIL checks remain
open. Serial-only success becomes `OPERATOR_REVIEW_REQUIRED` until the operator
evidence is reviewed.

Opt-in mutating commands that declare `recovery_command` now insert the
restore-safe-state command into the plan. The current recovery command is
`dirin 0xFFFF`.

## Checks Run

| Command | Result |
| --- | --- |
| `python -m py_compile tools/run_i2c_hil.py tools/check_hil_contract.py` | PASS |
| `python tools/run_i2c_hil.py --dry-run` | PASS; runner final verdict `INCOMPLETE` by design because no serial hardware was opened |
| `python tools/check_hil_contract.py` | PASS (`check_hil_contract: PASS`) |
| `python tools/check_core_timing_guard.py` | PASS (`Core framework guard PASSED`) |
| `python tools/check_cli_contract.py` | PASS (`CLI contract PASSED`) |
| `python tools/check_idf_example_contract.py` | PASS (`ESP-IDF example contract PASSED`) |
| `python scripts/generate_version.py check` | PASS (`Version.h` up to date) |
| `python -m platformio test -e native` | PASS; 126 test cases succeeded |
| `python -m platformio run -e esp32s2dev` | PASS; `esp32s2dev SUCCESS` |
| `python -m platformio run -e esp32s3dev` | PASS; `esp32s3dev SUCCESS` |
| `python -m platformio pkg pack` | PASS; generated `PCA9555-1.1.0.tar.gz` and it was removed after validation |
| `idf.py --version` | NOT RUN; command unavailable in this shell (`idf.py` not recognized) |
| `idf.py -C examples/esp_idf/basic set-target esp32s3 build` | NOT RUN because `idf.py` is unavailable |
| `idf.py -C examples/esp_idf/basic set-target esp32s2 build` | NOT RUN because `idf.py` is unavailable |
| `gh auth status` | NOT RUN; command unavailable in this shell (`gh` not recognized) |

## Dry-Run Result

PASS as a software planning check. The dry run emitted `summary.md`,
`summary.json`, `serial_transcript.txt`, and `operator_checklist.md` under
`hil_logs/i2c_20260531_163321/`. The runner final verdict was `INCOMPLETE`,
which is correct because no serial port or physical hardware was opened.

## Hardware Run Result

No physical HIL validation was performed. Hardware validation remains NOT RUN
until an operator provides serial port, board, wiring, power/load safety details,
and generated evidence artifacts.

## PR And CI Status

GitHub CLI status was not checked because `gh` is not installed or not on PATH
in this shell. CI status must be checked in the GitHub web UI after pushing, or
with `gh auth status` and PR checks on a machine where GitHub CLI is available.

## Remaining Blockers

- Real hardware HIL run has not been performed.
- Manual and visual checks remain `OPERATOR_CHECK_REQUIRED`.
- Fault injection and output-driving tests require explicit safe-fixture
  approval before execution.
- The broader working tree had unrelated uncommitted hardening changes at the
  start of this task, so final commit/push must be evaluated carefully.

## Auditor-Facing Verdict

`software-prepared only`

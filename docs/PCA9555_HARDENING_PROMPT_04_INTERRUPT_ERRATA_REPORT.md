# PCA9555 Hardening Prompt 04 Interrupt Errata Report

## Scope

- Branch: `hardening/pca9555-industry-readiness`
- Prompt: `04_interrupt_errata_hardening.md`
- Date: 2026-05-31

Prompt 04 was kept to INT servicing, errata behavior, and shared-bus compound
sequence serialization. It did not start Prompt 05 fault-injection expansion, pure
ESP-IDF component work, or hardware-validation work.

## Public APIs Added

- `Status readInputsAndClearInterrupt(uint16_t& value)`
- `Status clearInterrupts()`
- `Status applyInterruptErrataWorkaround()`

`readInputsAndClearInterrupt()` reads Input Port 0/1 in one pair read, clears both
PCA9555 input-port interrupt sources, and returns the combined 16-bit input value.

`clearInterrupts()` clears both input-port interrupt sources by reading both input
ports. It does not write to INT.

`applyInterruptErrataWorkaround()` writes the safe nonzero command pointer byte
directly for applications that need an explicit pointer-park operation.

## Errata Behavior

Preserved requirements:

- `Config::applyInterruptErrata` default remains `true`.
- `cmd::ERRATA_SAFE_CMD` remains `0x02`.
- The errata workaround is a command-only tracked write of `cmd::ERRATA_SAFE_CMD`.

Every typed input-read path and public direct input-register read path now routes through
one compound helper:

- `readInputs()`
- `readInputsAndClearInterrupt()`
- `clearInterrupts()`
- `readInput()`
- `readPin()`
- `readRegister()` / `readRegisters()` when the start register is Input Port 0/1
- `_applyConfig()` during `begin()` / `recover()`

If the input read fails, the pointer-park write is not attempted. If the input read
succeeds and the errata write fails, the API returns the errata write `Status`; returned
input data is valid and health is updated by the tracked errata write. Command-only errata
write failures do not mark hardware state dirty.

Port-specific behavior is explicit:

- `readInputs()`, `readInputsAndClearInterrupt()`, and `clearInterrupts()` clear both ports.
- `readInput(Port::PORT_0)` and pins `0-7` clear only Port 0.
- `readInput(Port::PORT_1)` and pins `8-15` clear only Port 1.
- Direct input-register reads clear the ports actually read by that register-pair access.

## Lock And Serialization Design

Optional framework-neutral hooks were added to `Config` and appended after existing fields
to preserve prior aggregate initialization order:

- `LockFn i2cLock`
- `UnlockFn i2cUnlock`
- `void* lockUser`

If either lock hook is set, both must be set or `begin()` returns `INVALID_CONFIG`.
`i2cLock()` receives `Config::i2cTimeoutMs`; a non-OK lock result means no lock was
acquired and no unlock is called.

When configured, the driver holds the lock across the full compound input-read plus
errata-write sequence and releases it on success, read failure, and errata failure.
Validation failures return before locking. If hooks are absent, the application bus manager
must provide equivalent serialization.

Transport callbacks invoked while this compound lock is held must not reacquire the same
non-recursive lock. This avoids deadlocking an application that adapts a single mutex to
both the compound hook and the low-level transport callback.

## ISR Contract

No ISR safety is claimed.

- INT pin ISR code should only set a flag or notify a task.
- Task/main context should call `readInputsAndClearInterrupt()` or `clearInterrupts()`.
- Public APIs that touch I2C are not ISR-safe.
- Shared buses require serialization of the compound input-read plus errata-write sequence.

## Tests Added

Native tests added or expanded in `test/test_basic.cpp`:

- `test_begin_rejects_partial_lock_hooks`
- `test_all_input_read_paths_write_exact_errata_command`
- `test_read_inputs_errata_write_failure_is_reported_and_updates_health`
- `test_read_inputs_read_failure_does_not_pointer_park`
- `test_read_inputs_and_clear_interrupt_returns_both_ports`
- `test_clear_interrupts_reads_both_ports_then_parks_pointer`
- `test_clear_interrupts_errata_disabled_reads_only`
- `test_read_input_port0_clears_only_port0_interrupt`
- `test_read_input_port1_clears_only_port1_interrupt`
- `test_apply_interrupt_errata_workaround_parks_pointer`
- `test_input_read_errata_lock_wraps_full_sequence`
- `test_input_read_errata_lock_releases_on_read_failure`
- `test_input_read_errata_lock_releases_on_errata_failure`
- `test_input_read_lock_failure_skips_i2c_and_unlock`
- `test_input_read_validation_failure_does_not_lock`
- `test_input_read_errata_lock_blocks_interleaved_external_read`

The fake bus now records command pointer state, simulated per-port interrupt pending flags,
lock/unlock events, and a fake interleaver attempt.

## Documentation Updated

- `README.md` documents INT as active-low/open-drain requiring a pull-up.
- `README.md` documents per-port input-read interrupt clearing behavior.
- `README.md` documents that output pins do not generate input-change interrupts.
- `README.md` recommends task/main-context `readInputsAndClearInterrupt()` or
  `clearInterrupts()` after an INT notification.
- `README.md` documents errata default behavior and `ERRATA_SAFE_CMD == 0x02`.
- `README.md` documents optional lock hooks, missing-hook external serialization, and
  the no-reacquire rule for callbacks under the compound lock.
- Doxygen comments in `Config.h` and `PCA9555.h` document the same contracts.

## Validation Results

| Command | Result |
|---|---|
| `git status --short` | Working tree dirty with related Prompt 01-04 changes; no unrelated user edits identified |
| `git branch --show-current` | `hardening/pca9555-industry-readiness` |
| `Test-Path docs/PCA9555_HARDENING_PROMPT_01_CORE_PORTABILITY_REPORT.md` | PASS: report exists |
| `Test-Path docs/PCA9555_HARDENING_PROMPT_02_DIRTY_STATE_REPORT.md` | PASS: report exists |
| `Test-Path docs/PCA9555_HARDENING_PROMPT_03_GLITCH_SAFE_DIRECTION_REPORT.md` | PASS: report exists |
| `python tools/check_core_timing_guard.py` | PASS: `Core framework guard PASSED` |
| `python scripts/generate_version.py check` | PASS: `Version.h` up to date |
| `python -m platformio test -e native` | PASS: 107 tests, 107 succeeded, duration `00:00:15.527` |
| `python -m platformio run -e esp32s2dev` | PASS: `SUCCESS`, duration `00:00:27.787` |
| `python -m platformio run -e esp32s3dev` | PASS: `SUCCESS`, duration `00:01:24.329` |
| `git diff --check` | PASS; only Git LF-to-CRLF warnings were printed |

## Commands Not Run

- No hardware commands were run.
- No pure ESP-IDF `idf.py` build was run. Prompt 06 covers pure ESP-IDF readiness.

## Subagent Findings

- `interrupt-contract-agent` confirmed existing both-port and per-port interrupt clearing
  behavior and identified missing public docs/tests for those distinctions.
- `errata-agent` confirmed `ERRATA_SAFE_CMD == 0x02`, default errata enablement, and missing
  exact-command/error-propagation tests.
- `locking-design-agent` recommended optional framework-neutral lock hooks held around only
  compound input-read plus errata-write sequences.
- `fault-test-agent` recommended transaction-order, failure-path, lock-release, and
  interleaving-prevention tests; those were added.
- `integration-review-agent` found two blocking issues: lock contract ambiguity that could
  deadlock with non-recursive locks, and lock fields inserted into the middle of the public
  aggregate `Config`. Both were fixed before final validation.

## Remaining Work

- Prompt 05: broader fault-injection matrix.
- Prompt 06: pure ESP-IDF component build and reproducibility.
- Prompt 07: release gates and hardware validation documentation.
- Prompt 08: final integration and merge-readiness assessment.

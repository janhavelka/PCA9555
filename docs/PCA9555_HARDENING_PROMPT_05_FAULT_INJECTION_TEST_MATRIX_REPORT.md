# PCA9555 Hardening Prompt 05 Fault-Injection Test Matrix Report

## Scope

- Branch: `hardening/pca9555-industry-readiness`
- Prompt: `05_fault_injection_matrix.md`
- Date: 2026-05-31

Prompt 05 was kept to the native test matrix, fake transport hardening, and a
small diagnostic documentation clarification. It did not add ESP-IDF component
metadata, start pure ESP-IDF work, or claim hardware validation.

## Start Checks

| Check | Result |
|---|---|
| `git status --short` | Dirty with related Prompt 01-04 hardening changes already present, plus Prompt 05 test/report work |
| `git branch --show-current` | `hardening/pca9555-industry-readiness` |
| Prompt 01 report exists | PASS |
| Prompt 02 report exists | PASS |
| Prompt 03 report exists | PASS |
| Prompt 04 report exists | PASS |

No unrelated dirty work was identified. No commit was made because this prompt
did not ask for commit/sync.

## Fake Transport Changes

`test/test_basic.cpp` fake-bus coverage was extended with:

- `FakeTransaction` records for transaction type, callback address, timeout,
  full TX bytes, requested RX length, RX bytes produced, returned `Status`,
  command pointer before/after, and data bytes applied to hardware.
- Compatibility mirrors for existing tests: `transactionType`,
  `transactionReg`, `transactionData0`, `transactionData1`, and
  `transactionLen` remain available.
- Optional address enforcement and device-present simulation for valid-address,
  wrong-address, and absent-device cases.
- Pair-bounded register stepping through `fakePairedRegisterAt()`.
- Input-register read modeling as external input sense XOR polarity inversion.
- Read-only input register behavior in the fake: attempted writes do not mutate
  Input Port 0/1.
- Failure modes for fail-before-apply writes, partial apply-then-fail writes,
  short writes, short reads, unavailable read data, NACK address, NACK data,
  timeout, bus, and generic I2C errors.
- External mutation helpers including `fakePowerCycle()`, `fakeSetInputs()`,
  `fakeDrivePin()`, `fakeMutateOutputLatch()`, `fakeMutateConfiguration()`,
  and `fakeMutatePolarity()`.

## Test Matrix Added

Native test count increased from the Prompt 04 baseline of 107 to 126.

New or materially expanded Prompt 05 tests include:

- Address matrix:
  - `test_begin_accepts_all_pca9555_address_pins_and_logs_callback_address`
  - `test_begin_rejects_address_matrix_without_touching_bus`
  - `test_begin_reports_device_absent_or_wrong_address_as_not_found`
- Probe/error mapping:
  - `test_probe_error_matrix_maps_to_device_not_found_preserving_detail`
- Configuration-default and input identity behavior:
  - `test_begin_checks_both_configuration_defaults_not_input_identity`
- Fake transaction log and register command matrix:
  - `test_fake_transaction_log_records_address_payload_rx_status_and_pointer`
  - `test_direct_register_command_matrix_reads_and_writes_exact_commands`
  - `test_register_pair_auto_increment_wrap_matrix_for_all_pairs`
  - `test_fake_bus_keeps_input_register_pair_read_only`
- Dirty-state and transport-fault matrix:
  - `test_failed_begin_apply_partial_write_marks_dirty_and_uninitialized`
  - `test_transport_read_error_matrix_updates_health_without_dirty_state`
  - `test_transport_write_error_matrix_updates_health_and_marks_dirty`
  - `test_partial_pair_write_all_bytes_then_error_marks_dirty_without_cache_sync`
  - `test_short_write_failures_record_command_boundary`
  - `test_short_read_and_unavailable_data_do_not_sync_cache_on_failure`
  - `test_failure_threshold_enters_offline_and_blocks_bus_until_recover`
- Example transport read-path errors:
  - `test_example_transport_write_read_maps_wire_errors_and_short_read`
- Polarity/input/output semantics:
  - `test_polarity_inverts_input_sense_only_not_output_latch_or_direction`
  - `test_output_latch_writes_do_not_mutate_input_sense`

Existing Prompt 03 and Prompt 04 tests continue to cover glitch-safe direction
ordering and interrupt/errata behavior.

## Coverage Notes

- `probe()` keeps using raw I2C with no health tracking. Transport failures are
  mapped to `DEVICE_NOT_FOUND`; the original transport detail code is preserved.
  README now documents this mapping.
- Copy/move safety is covered with compile-time static assertions in
  `test/test_basic.cpp`.
- CI already runs native tests through `.github/workflows/ci.yml`; plain
  `pio run` remains configured for the default embedded environment, not native.
- No production driver behavior changes were required by the Prompt 05 matrix.
  The only non-test source touched by this prompt was the README diagnostic
  clarification.

## Bugs Found And Fixed

- Fixed the native fake bus so writes targeting Input Port 0/1 do not mutate
  read-only input register state.
- Added missing probe error-mapping coverage after integration review.

No production driver bug was found during Prompt 05.

## Validation Results

| Command | Result |
|---|---|
| `python -m platformio test -e native` before Prompt 05 changes | PASS: 107 tests, 107 succeeded, duration `00:00:03.751` |
| `python tools/check_core_timing_guard.py` | PASS: `Core framework guard PASSED` |
| `python scripts/generate_version.py check` | PASS: `Version.h` up to date |
| `python tools/check_cli_contract.py` | PASS: `CLI contract PASSED` |
| `python -m platformio test -e native` final | PASS: 126 tests, 126 succeeded, duration `00:00:03.126` |
| `python -m platformio run -e esp32s2dev` | PASS: `SUCCESS`, duration `00:00:20.295` |
| `python -m platformio run -e esp32s3dev` first final attempt | FAIL: framework object compile stopped at `.pio\build\esp32s3dev\FrameworkArduino\esp32-hal-touch.c.o`, duration `00:00:17.217`, no library diagnostic in captured output |
| `python -m platformio run -e esp32s3dev` rerun | PASS: `SUCCESS`, duration `00:00:22.019` |
| `git diff --check` | PASS; only Git LF-to-CRLF warnings were printed |

## Commands Not Run

- No hardware commands were run; no hardware validation is claimed.
- No `idf.py` or pure ESP-IDF CI build was run. Prompt 06 covers pure ESP-IDF
  component readiness and reproducibility.

## Subagent Findings

- `fake-bus-agent` identified the lossy transaction log, missing address
  assertions, missing short read/write modes, and need for external mutation
  helpers.
- `register-matrix-agent` identified missing full address, command byte,
  pair-wrap, POR/default, polarity, and input/output independence coverage.
- `transport-error-agent` identified missing systematic read/write transport
  error matrices, short read/write coverage, and threshold-to-offline tests.
- `docs-test-agent` confirmed native tests are already wired into CI and called
  out the need to clarify input registers as pin-dependent rather than fixed
  identity/default registers.
- `integration-review-agent` identified the missing probe matrix/report
  deliverable and the fake-bus input-register write modeling gap; both were
  addressed before final validation.

## Remaining Work

- Prompt 06: pure ESP-IDF component build and reproducibility.
- Prompt 07: release gates and hardware validation documentation.
- Prompt 08: final integration, merge-readiness assessment, and release
  packaging guidance.

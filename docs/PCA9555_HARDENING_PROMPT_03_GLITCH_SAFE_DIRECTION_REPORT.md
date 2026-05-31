# PCA9555 Hardening Prompt 03 Glitch-Safe Direction Report

## Scope

- Branch: `hardening/pca9555-industry-readiness`
- Prompt: `03_glitch_safe_direction_apis.md`
- Date: 2026-05-31

Prompt 03 was kept to runtime GPIO direction safety. It did not implement interrupt/errata
locking, expanded fault-injection work beyond the direction sequencing cases, pure ESP-IDF
component work, or hardware-validation gates.

## New And Changed APIs

New public APIs:

- `enum class Direction : uint8_t { INPUT_MODE, OUTPUT_MODE }`
- `Status preloadOutput(Pin pin, bool high)`
- `Status preloadOutputs(uint16_t mask, uint16_t values)`
- `Status setDirection(Pin pin, Direction direction)`
- `Status configureOutputs(uint16_t outputMask, uint16_t outputValues)`

Changed public API behavior:

- `setConfiguration()` now force-preloads cached output latch values before any
  input-to-output bit transition.
- `setPortConfiguration()` now force-preloads cached output latch values before any
  input-to-output bit transition.
- `setPinDirection(pin, false)` now routes through the same preload-before-config path.
- `configureOutputBits(mask)` is preserved as a legacy helper and now preloads cached
  latch values before clearing actual input-to-output transition bits, while preserving the
  prior no-op/no-I2C behavior for pins already configured as outputs.

No public APIs were removed.

## Ordering Guarantees

For input-to-output transitions, the driver now guarantees:

1. The selected Output Port latch value is written first.
2. The Configuration bit is cleared only after the preload write succeeds.
3. If preload fails, no direction/configuration write is attempted.
4. If the direction/configuration write fails after preload succeeds, the original
   transport error is returned and `hardwareStateDirty()` is set by the Prompt 02 policy.
   The latch may have been updated while direction bits may or may not have changed.

`configureOutputs(mask, values)` is the preferred bulk API. It always calls
`preloadOutputs(mask, values)` when `mask != 0`; then it clears the selected configuration
bits only if the cached direction state still requires a change.

`configureOutputBits(mask)` first computes `currentConfig & mask`, so it preloads and
configures only pins that are currently inputs. A mask that selects only pins already
configured as outputs returns `Status::Ok()` without I2C, preserving the legacy bit-helper
no-op contract.

`preloadOutput()` and `preloadOutputs()` force an output-latch I2C write even when the
cached latch already matches the requested value. This supports recovery from external
mutation, reset, diagnostics, or dirty-state uncertainty.

Output-to-input transitions write only the Configuration register. They are documented as
possibly triggering PCA9555 false-interrupt behavior when sampled input state differs from
the previous input-register state.

`begin()` and `recover()` preserve the safe application order:

```text
output latch -> polarity -> configuration -> input read -> errata workaround
```

## Legacy API Behavior

The old direction APIs remain source-compatible:

- `setPinDirection(Pin pin, bool input)`
- `setConfiguration(const PortData& data)`
- `setPortConfiguration(Port port, uint8_t value)`
- `configureOutputBits(uint16_t mask)`

When those APIs cannot receive an explicit desired output value, they use the cached output
latch as the desired preload. Documentation recommends `configureOutputs()` for multi-pin
runtime output enabling and `preloadOutput()` plus `setDirection()` for single-pin call
sites where the desired initial level should be explicit.

Direct `writeRegister()` / `writeRegisters()` remain diagnostic/advanced APIs. They can
write configuration registers directly and remain covered by dirty-state diagnostics.

## Tests Added

Native fake-bus transaction logging now records attempted write and write-read operations so
tests can assert ordering, not only final register state.

Prompt 03 tests added in `test/test_basic.cpp`:

- `test_begin_ordering_remains_safe`
- `test_configure_outputs_writes_latch_before_config`
- `test_single_pin_output_transition_writes_latch_before_config`
- `test_forced_preload_writes_even_when_cache_matches`
- `test_failed_preload_does_not_change_direction`
- `test_failed_direction_after_preload_marks_dirty`
- `test_output_to_input_transition_writes_config_only`
- `test_configure_output_bits_no_op_for_existing_outputs`

Existing normal pin write/read, output latch, direction, polarity, direct register,
recovery, health, and Prompt 02 dirty-state tests still pass.

## Documentation Updated

- `README.md` now explains Input Port vs Output Port latch vs Configuration register
  semantics.
- `README.md` recommends `configureOutputs()` for bulk runtime output enabling.
- `README.md` documents preload failure and post-preload direction-write failure behavior.
- `README.md` warns that output-to-input transitions can cause PCA9555 false-interrupt
  behavior.
- Doxygen comments in `include/PCA9555/PCA9555.h` document the same API contracts and
  direction-change warnings.

## Validation Results

| Command | Result |
|---|---|
| `git status --short` | Working tree dirty with related Prompt 01, 02, and 03 changes; no unrelated user edits identified |
| `git branch --show-current` | `hardening/pca9555-industry-readiness` |
| `Test-Path docs/PCA9555_HARDENING_PROMPT_01_CORE_PORTABILITY_REPORT.md` | PASS: report exists |
| `Test-Path docs/PCA9555_HARDENING_PROMPT_02_DIRTY_STATE_REPORT.md` | PASS: report exists |
| `python tools/check_core_timing_guard.py` | PASS: `Core framework guard PASSED` |
| `python scripts/generate_version.py check` | PASS: `Version.h` up to date |
| `python -m platformio test -e native` | PASS: 91 tests, 91 succeeded, duration `00:00:12.392` |
| `python -m platformio run -e esp32s2dev` | PASS: `SUCCESS`, duration `00:01:10.142` |
| `python -m platformio run -e esp32s3dev` | PASS: `SUCCESS`, duration `00:00:34.192` |
| `git diff --check` | PASS; only Git LF-to-CRLF warnings were printed |

## Commands Not Run

- No hardware commands were run.
- No pure ESP-IDF `idf.py` build was run. Prompt 06 covers pure ESP-IDF readiness.

## Subagent Findings

- `direction-api-agent` identified unsafe runtime direction paths in
  `setPinDirection()`, `configureOutputBits()`, `setConfiguration()`, and
  `setPortConfiguration()`; all now route through preload-before-config handling.
- `electrical-safety-agent` confirmed that Output Port reads return latch state, not pin
  voltage, and that output enabling must preload latches before clearing direction bits.
- `test-sequencing-agent` recommended transaction-log ordering tests; the fake bus now
  records attempted writes and write-reads to prove those sequences.
- `integration-review-agent` found that `configureOutputBits()` initially forced preload I2C
  even when selected pins were already outputs; fixed by computing an input-to-output
  transition mask first and adding a regression test.
- `integration-review-agent` found stale dirty-state Doxygen wording; fixed to mention that
  successful `begin()` also reconciles dirty state.
- `integration-review-agent` noted non-copyable/non-movable driver instances. That behavior
  was introduced by Prompt 01, not Prompt 03, and was left unchanged here.

## Remaining Work

- Prompt 04: interrupt locking / compound transaction hardening around input read plus
  errata pointer park.
- Prompt 05: broader fault-injection matrix.
- Prompt 06: pure ESP-IDF component build and reproducibility.
- Prompt 07: release gates and hardware validation documentation.
- Prompt 08: final integration and merge-readiness assessment.

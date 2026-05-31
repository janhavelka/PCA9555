# PCA9555 Hardening Prompt 02 Dirty-State Report

## Scope

- Branch: `hardening/pca9555-industry-readiness`
- Prompt: `02_dirty_state_partial_write.md`
- Date: 2026-05-31

Prompt 02 was kept to dirty-state diagnostics and partial-write semantics. It did not add glitch-safe direction APIs, interrupt-locking changes, ESP-IDF component work, or hardware-validation docs.

## Dirty-State API Added

Public diagnostics added to `PCA9555`:

- `bool hardwareStateDirty() const`
- `Status hardwareStateDirtyError() const`

`SettingsSnapshot` now includes:

- `bool hardwareStateDirty`
- `Status hardwareStateDirtyError`

The dirty error stores the original transport `Status` that marked the state dirty. The failed API still returns that same original status; no generic dirty-state error replaces `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, or other useful transport errors.

## Exact Semantics

- Failed argument/config validation does not mark dirty.
- `NOT_INITIALIZED`, `BUSY`, and `IN_PROGRESS` do not mark dirty.
- Failed reads do not mark dirty.
- Failed tracked writes with at least one register data byte mark dirty conservatively.
- Failed command-only writes, such as the interrupt errata pointer-park command, do not mark register/cache dirty because no register data byte is intended.
- Failed write APIs do not update cached desired output, configuration, or polarity state.
- Direct register write failures mark dirty because hardware may have accepted a data byte while cache stayed unchanged.
- Dirty state survives unrelated successful reads.
- Dirty state survives failed or partially successful recovery.
- Dirty state survives failed `begin()` validation.

## Recovery And Sync Policy

No new sync API was added.

`recover()` is the primary reconciliation path. It performs a tracked configuration-register read, reapplies cached desired output, polarity, and configuration registers, clears pending interrupts, and applies the interrupt errata workaround. Dirty state clears only after the full `recover()` sequence succeeds.

A later successful `begin()` also clears dirty state after verifying the device and applying the requested configuration. Failed `begin()` validation preserves existing dirty diagnostics.

## Fake Bus Changes

The native `FakeBus` now supports partial-write modeling:

- fail before applying any data byte;
- apply command plus the first data byte, then return `I2C_NACK_DATA`;
- apply command plus the first data byte, then return `I2C_TIMEOUT`;
- apply command plus the first data byte, then return `I2C_BUS`;
- apply a one-byte register write, then return failure;
- record whether any data byte reached the fake hardware;
- preserve pair auto-increment behavior for even and odd register-pair starts.

## Tests Added

Dirty-state tests added in `test/test_basic.cpp` cover:

- validation failure does not mark dirty;
- failed read does not mark dirty;
- fail-before-apply write preserves cache and marks dirty conservatively;
- partial output-pair write marks dirty and preserves original error;
- partial configuration-pair write marks dirty and preserves original error;
- partial polarity-pair write marks dirty and preserves original error;
- direct single-register write failure marks dirty;
- direct odd-start paired-register write failure marks dirty;
- dirty fields appear in `SettingsSnapshot`;
- dirty survives unrelated successful reads;
- dirty clears after full successful `recover()`;
- dirty does not clear after partial `recover()`;
- failed `begin()` validation preserves existing dirty state.

Existing normal lifecycle, register, health, and example transport tests still pass.

## Files Changed For Prompt 02

- `README.md`
- `include/PCA9555/PCA9555.h`
- `src/PCA9555.cpp`
- `test/test_basic.cpp`
- `docs/PCA9555_HARDENING_PROMPT_02_DIRTY_STATE_REPORT.md`

Pre-existing uncommitted Prompt 01 files remain in the working tree:

- `include/PCA9555/Config.h`
- `tools/check_core_timing_guard.py`
- `docs/PCA9555_HARDENING_PROMPT_01_CORE_PORTABILITY_REPORT.md`
- `docs/PCA9555_HARDENING_BACKLOG.md`
- `docs/PCA9555_INDUSTRY_READINESS_AUDIT.md`

## Validation Results

| Command | Result |
|---|---|
| `git status --short` | Working tree dirty with Prompt 01 and Prompt 02 changes; no unrelated user edits identified |
| `git branch --show-current` | `hardening/pca9555-industry-readiness` |
| `Test-Path docs/PCA9555_HARDENING_PROMPT_01_CORE_PORTABILITY_REPORT.md` | PASS: `exists` |
| `python tools/check_core_timing_guard.py` | PASS: `Core framework guard PASSED` |
| `python scripts/generate_version.py check` | PASS: `Version.h` up to date |
| `python -m platformio test -e native` | PASS: 83 tests, 83 succeeded, duration `00:00:02.896` |
| `python -m platformio run -e esp32s2dev` | PASS: `SUCCESS`, duration `00:00:22.672` |
| `python -m platformio run -e esp32s3dev` | PASS: `SUCCESS`, duration `00:00:25.907` |
| `python tools/check_cli_contract.py` | PASS: `CLI contract PASSED` |
| `python -m platformio pkg pack` | PASS: wrote `PCA9555-1.1.0.tar.gz`; generated tarball removed afterward |
| `git diff --check` | PASS; only Git LF-to-CRLF warnings were printed |

## Commands Not Run

- No hardware commands were run.
- No pure ESP-IDF `idf.py` build was run. Prompt 06 covers pure ESP-IDF readiness.

## Remaining Work

- Prompt 03: glitch-safe direction APIs for runtime load transitions.
- Prompt 04: interrupt locking / compound transaction hardening around input read plus errata pointer park.
- Prompt 05: expanded fault-injection matrix beyond this focused partial-write model.
- Prompt 06: pure ESP-IDF component build and reproducibility.
- Prompt 07: release gates and hardware validation documentation.
- Prompt 08: final integration and merge-readiness assessment.

## Integration Review

The Prompt 02 integration-review agent initially found that `begin()` validation failures could clear existing dirty state. That blocker was fixed, covered by regression test, and re-reviewed with no remaining blocking findings.

No commit was made because the user did not request commit/sync.

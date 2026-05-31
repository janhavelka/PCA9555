# PCA9555 Hardening Prompt 01 Core Portability Report

## Scope

- Branch: `hardening/pca9555-industry-readiness`
- Prompt: `01_core_portability_copy_move.md`
- Date: 2026-05-31

Prompt 01 was kept narrow: core portability, injected timebase behavior, copy/move deletion, public contract documentation, guard coverage, and native tests. Dirty-state tracking, glitch-safe direction APIs, interrupt-locking changes, and ESP-IDF component work were not implemented.

## Files Changed

- `README.md`
- `include/PCA9555/Config.h`
- `include/PCA9555/PCA9555.h`
- `src/PCA9555.cpp`
- `test/test_basic.cpp`
- `tools/check_core_timing_guard.py`
- `docs/PCA9555_HARDENING_PROMPT_01_CORE_PORTABILITY_REPORT.md`

Existing untracked audit files preserved:

- `docs/PCA9555_HARDENING_BACKLOG.md`
- `docs/PCA9555_INDUSTRY_READINESS_AUDIT.md`

## Core Portability

- `src/PCA9555.cpp` no longer includes `Arduino.h`.
- `_nowMs()` no longer calls `millis()` or any platform clock.
- No `Arduino.h`, `Wire.h`, ESP-IDF, FreeRTOS, `Serial`, `String`, `millis()`, or `delay()` usage remains in `include/` or `src/`.
- Arduino behavior remains example-only through injected adapters and explicit example/application callbacks.

## Null `nowMs` Behavior

- `Config::nowMs` remains optional.
- If `Config::nowMs == nullptr`, health timestamps remain `0`.
- Ordinary public APIs do not fail just because no timebase callback is supplied.
- Explicit fake `nowMs` callbacks still update `lastOkMs()` and `lastErrorMs()`.

## Copy/Move Deletion

`PCA9555::PCA9555` now explicitly deletes:

- copy constructor
- copy assignment
- move constructor
- move assignment

Native compile-time `static_assert` coverage verifies the driver type is not copyable or movable.

## Documentation Updated

- README top-level wording changed from "production-grade" to production-oriented / framework-neutral with industry-readiness hardening still in progress.
- README quick start now injects an explicit `nowMs` callback for Arduino usage.
- README behavioral contracts now document:
  - framework-neutral core
  - external I2C ownership
  - no internal locking
  - non-thread-safe / non-reentrant instances
  - no ISR-safe public I2C APIs
  - required external shared-bus serialization
  - non-recursive, synchronous transport callbacks
  - optional `Config::nowMs`
  - example helpers as diagnostic/bring-up project glue
- Public Doxygen comments in `Config.h` and `PCA9555.h` now document transport, timebase, thread/ISR, and serialization contracts.

## Tests Added Or Updated

- Replaced the old hidden platform-clock fallback test with `test_null_now_ms_keeps_health_timestamps_zero`.
- Added `test_now_ms_callback_updates_health_timestamps`.
- Added compile-time static assertions for disabled copy/move operations.
- Existing lifecycle, health, register, bit manipulation, and example transport tests still pass.

## Guard Coverage

`tools/check_core_timing_guard.py` now fails if core source or public headers contain forbidden framework tokens, including:

- `Arduino.h`
- `Wire.h`
- `freertos/`
- `driver/i2c`
- `esp_`
- `millis(`
- `delay(`
- `vTaskDelay`
- `Serial`
- `String`

The guard scans raw preprocessor include lines separately so quoted includes such as `"Arduino.h"` are caught, while normal code scanning strips comments and string literals to reduce false positives.

## Validation Results

| Command | Result |
|---|---|
| `git status --short` | Initial status contained only existing untracked audit docs; after implementation see working tree notes below. |
| `git branch --show-current` | `hardening/pca9555-industry-readiness` |
| `python tools/check_core_timing_guard.py` | PASS: `Core framework guard PASSED` |
| `python scripts/generate_version.py check` | PASS: `Version.h` up to date |
| `python -m platformio test -e native` | PASS: 70 tests, 70 succeeded |
| `python -m platformio run -e esp32s2dev` | PASS: `SUCCESS`, duration `00:00:29.753` |
| `python -m platformio run -e esp32s3dev` | First attempt failed in framework object `esp32-hal-uart.c.o` with no compiler diagnostic in captured output; exact rerun passed. |
| `python -m platformio run -e esp32s3dev` rerun | PASS: `SUCCESS`, duration `00:00:35.379` |
| `python tools/check_cli_contract.py` | PASS: `CLI contract PASSED` |
| `python -m platformio pkg pack` | PASS: wrote `PCA9555-1.1.0.tar.gz`; generated tarball removed afterward |
| `git diff --check` | PASS; only Git LF-to-CRLF warnings were printed |

## Commands Not Run

- No hardware commands were run.
- No pure ESP-IDF `idf.py` build was run. Prompt 06 covers pure ESP-IDF readiness.

## Remaining Work For Later Prompts

- Prompt 02: dirty-state / hardware-cache divergence diagnostics and partial-write handling.
- Prompt 03: glitch-safe direction APIs for runtime load transitions.
- Prompt 04: interrupt errata and interrupt-service hardening beyond current command-byte workaround.
- Prompt 05: expanded fault-injection matrix.
- Prompt 06: pure ESP-IDF component build and reproducibility.
- Prompt 07: release gates and hardware validation documentation.
- Prompt 08: final integration and merge-readiness assessment.

## Working Tree Notes

No commit was made because the user did not request commit/sync. The working tree has Prompt 01 source/docs changes plus the previously untracked audit docs.

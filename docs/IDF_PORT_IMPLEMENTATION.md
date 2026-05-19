# PCA9555 ESP-IDF Port Implementation Notes

Date: 2026-05-19.
Branch: `feature/pca9555-idf-port`.

## Scope

- Kept `include/PCA9555/` and `src/PCA9555.cpp` as a framework-neutral driver
  core with application-owned I2C callbacks.
- Added ESP-IDF component metadata and a native IDF entry point for the full
  bring-up CLI.
- Added an example-only ESP-IDF compatibility layer so the Arduino and ESP-IDF
  examples share one CLI command implementation.
- Preserved the full PCA9555 example surface across both frameworks: scan,
  diagnostics, settings, health, input sense, output latch, direction, polarity,
  register access, mask helpers, self-test, stress, sweep, walk, and pattern
  flows.

## Files Added

- `CMakeLists.txt`
- `idf_component.yml`
- `examples/common/IdfArduinoCompat.h`
- `examples/espidf_basic/CMakeLists.txt`
- `examples/espidf_basic/main/CMakeLists.txt`
- `examples/espidf_basic/main/main.cpp`

## Audit Resolution

- `docs/IDF_PORT.md` blocker: missing root `CMakeLists.txt`.
  - Resolved with an IDF component that builds `src/PCA9555.cpp` and exports
    `include/`.
- `docs/IDF_PORT.md` blocker: missing `idf_component.yml`.
  - Resolved with metadata for ESP32-S2/S3 and IDF `>=6.0.1`.
- `docs/IDF_PORT.md` blocker: Arduino-only default timing fallback.
  - Resolved with `esp_timer_get_time()` for ESP-IDF builds when
    `Config::nowMs` is not provided.
- `docs/IDF_PORT.md` blocker: missing IDF example with Arduino feature parity.
  - Resolved with `examples/espidf_basic`, which includes the same
    `examples/01_basic_bringup_cli/main.cpp` command implementation under
    `PCA9555_EXAMPLE_PLATFORM_IDF=1`.
  - The IDF shim supplies the Arduino-shaped console, GPIO, timing, and
    `TwoWire` adapter surface through native ESP-IDF APIs.
- Arduino-ESP32 pitfall:
  - Do not infer native IDF mode from `ESP_PLATFORM`; Arduino-ESP32 defines it
    too. The shared CLI uses the explicit `PCA9555_EXAMPLE_PLATFORM_IDF` flag.

## Remaining Hardware Checks

- Build the IDF example for `esp32s3` and `esp32s2`; this shell did not have
  `idf.py` on PATH during the implementation pass.
- Run scan/probe and disconnected-device timeout checks on hardware through
  both Arduino and ESP-IDF entry points.
- Verify input sense, output latch, direction, polarity, sweep, walk, pattern,
  stress, and bus-recovery flows on the target board.

## Verification

- `python -m platformio test -e native`: passed.
- `python -m platformio run -e esp32s3dev`: passed.
- `python -m platformio run -e esp32s2dev`: passed.
- `python tools/check_cli_contract.py`: passed.
- `python tools/check_core_timing_guard.py`: passed.
- `git diff --check`: passed during the implementation pass.

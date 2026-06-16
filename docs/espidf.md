# ESP-IDF Notes

The PCA9555 core library is framework-neutral. Public headers and `src/` do not
include Arduino or ESP-IDF framework headers; all hardware access is supplied
through `Config` callbacks.

## Component Boundary

The root `CMakeLists.txt` registers the library as an ESP-IDF component that
compiles `src/PCA9555.cpp` and exposes `include/`. `idf_component.yml` must not
declare Arduino or Wire dependencies, and its version must match
`library.json`.

When `Config::nowMs` is null, health timestamps remain `0`. Framework time
sources belong in examples or application glue.

## Native Example

`examples/espidf_basic` is a native ESP-IDF application:

- entry point is `app_main()`
- I2C uses `driver/i2c_master.h`
- the example owns a persistent `i2c_master_dev_handle_t`
- timestamps use `esp_timer_get_time()` through `Config::nowMs`
- delays use `vTaskDelay()`
- console input uses fixed command buffers and `fgets()`

The Arduino and ESP-IDF examples share a command contract, not implementation
source. The ESP-IDF example must not include Arduino sources, `Wire`, `String`,
`Serial`, compatibility facades, or the Arduino CLI source file.

## Confirmation Guard

The ESP-IDF CLI requires a final `confirm` suffix before any command that:

- drives outputs
- changes direction
- changes polarity
- writes raw registers
- runs output patterns, sweep, walk, self-test, recovery, or stress flows

Unconfirmed guarded commands print the pending change, why confirmation is
required, and the exact confirmed command form. This keeps the native example
usable for bring-up without silently mutating hardware.

## Static Checks

`tools/check_idf_example_contract.py` enforces:

- native ESP-IDF API usage
- component metadata and version alignment
- absence of Arduino/Wire compatibility tokens
- command-surface parity with the Arduino CLI
- confirmation guard wording and command coverage

The check proves static contract coverage only. ESP-IDF hardware validation is
pending until target PCA9555 hardware is tested and recorded in
[hardware_validation.md](hardware_validation.md).

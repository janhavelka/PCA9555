# PCA9555 ESP-IDF Port

Scope: keep the PCA9555 driver usable from both Arduino/PlatformIO and pure
ESP-IDF while preserving the same bring-up example functionality.

## Result

- Core driver remains framework-neutral for I2C ownership. All bus access still
  goes through `Config::i2cWrite` and `Config::i2cWriteRead`.
- `src/PCA9555.cpp` no longer requires Arduino headers for ESP-IDF builds.
- If `Config::nowMs` is not supplied, Arduino/native-test builds use
  `millis()` and ESP-IDF builds use `esp_timer_get_time() / 1000`.
- Root `CMakeLists.txt` and `idf_component.yml` make the library consumable as
  an ESP-IDF component.
- `examples/espidf_basic` builds the same CLI implementation used by
  `examples/01_basic_bringup_cli`.

## Shared Example Strategy

The Arduino CLI is intentionally the source of truth. The ESP-IDF example sets
`PCA9555_EXAMPLE_PLATFORM_IDF=1`, includes `examples/common/IdfArduinoCompat.h`,
defines `Serial` and `Wire`, then includes the Arduino CLI source.

This keeps these flows aligned across both frameworks:

- scan and bus diagnostics
- settings and health views
- input/output readback
- output latch writes
- direction and polarity commands
- direct register access
- mask helpers
- self-test
- stress and stress_mix
- sweep, walk, and pattern commands

## ESP-IDF Example Glue

`examples/common/IdfArduinoCompat.h` is example-only. It is not part of the
driver API.

It provides:

- `millis`, `micros`, `delay`, `delayMicroseconds`, and `yield`
- GPIO helpers used by bus recovery
- a fixed-capacity `String` subset used by the CLI parser
- a nonblocking stdin/stdout `Serial` replacement
- a `TwoWire`-shaped adapter backed by ESP-IDF v6 `driver/i2c_master.h`
- direct `writeStatus` and `writeReadStatus` helpers used by
  `examples/common/I2cTransport.h` under ESP-IDF

The adapter maps ESP-IDF errors to library `Status` values:

- `ESP_OK` -> `Status::Ok()`
- `ESP_ERR_TIMEOUT` -> `Err::I2C_TIMEOUT`
- `ESP_ERR_INVALID_ARG` -> `Err::INVALID_PARAM`
- `ESP_ERR_INVALID_RESPONSE` / `ESP_ERR_NOT_FOUND` -> `Err::I2C_BUS`
- other errors -> `Err::I2C_ERROR`

Timeouts are clamped before passing into ESP-IDF transfer APIs so an overflow
cannot become an infinite wait.

## Component Files

Core component:

```cmake
idf_component_register(
  SRCS "src/PCA9555.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_timer
)
```

Example component:

```cmake
idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "." "../../common" "../../.."
  REQUIRES PCA9555 esp_driver_i2c esp_driver_gpio esp_timer freertos vfs
)
```

The IDF example targets ESP32-S2 and ESP32-S3 and requires ESP-IDF `>=6.0.1`.

## Remaining Integration Notes

- Applications still own SDA/SCL pins, pull-ups, I2C clock, and bus lifetime.
  The library does not create buses or devices.
- A real ESP-IDF application can either reuse the example adapter or provide a
  smaller project-specific adapter directly from its own `i2c_master_dev_handle_t`.
- IDF does not reliably expose address-NACK versus data-NACK through the simple
  transfer APIs used here, so generic bus errors are reported as `Err::I2C_BUS`.
- The CLI shim is intentionally narrow. If the Arduino example starts using more
  of Arduino `String`, `Print`, or `TwoWire`, extend the shim in the same commit
  as the example change.

## Validation

Completed locally:

- `python -m platformio test -e native`
- `python -m platformio run -e esp32s3dev`
- `python -m platformio run -e esp32s2dev`
- `python tools/check_cli_contract.py`
- `python tools/check_core_timing_guard.py`
- `python scripts/generate_version.py check`
- `doxygen Doxyfile`

`tools/check_cli_contract.py` verifies both the advertised Arduino CLI
command/help surface and the ESP-IDF wrapper contract: platform macro,
`IdfArduinoCompat.h`, shared source include, `app_main()`, and required IDF
CMake dependencies.

Pending in this shell:

- `idf.py build` for `examples/espidf_basic`

`idf.py` was not available on PATH during this audit, so the ESP-IDF example is
implemented and documented but still needs a real ESP-IDF toolchain build before
release.

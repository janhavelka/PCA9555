# PCA9555 Hardening Prompt 06 ESP-IDF Report

## Scope

- Branch: `hardening/pca9555-industry-readiness`
- Prompt: `06_esp_idf_component_build_reproducibility.md`
- Date: 2026-05-31

Prompt 06 was kept to ESP-IDF component packaging, a pure ESP-IDF diagnostic
example, and build reproducibility. It did not start Prompt 07 release gates or
hardware-validation work. No hardware validation is claimed.

## Start Checks

| Check | Result |
|---|---|
| `git status --short` | Dirty with related Prompt 01-05 hardening changes already present |
| `git branch --show-current` | `hardening/pca9555-industry-readiness` |
| Prompt 01 report exists | PASS |
| Prompt 02 report exists | PASS |
| Prompt 03 report exists | PASS |
| Prompt 04 report exists | PASS |
| Prompt 05 report exists | PASS |

No unrelated dirty work was identified. No commit was made because this prompt
did not ask for commit/sync.

## ESP-IDF Component Metadata

Added root ESP-IDF component files:

- `CMakeLists.txt`
- `idf_component.yml`

The root component registers only `src/PCA9555.cpp` and exposes `include/`.
It does not declare Arduino, `Wire`, ESP-IDF driver, FreeRTOS, logging, or
platform timing dependencies in the core component.

`idf_component.yml` declares:

- version `1.1.0`, checked against `library.json` by
  `tools/check_idf_example_contract.py`.
- description `Framework-neutral PCA9555 16-bit I/O expander driver.`
- targets `esp32s2` and `esp32s3`.
- ESP-IDF compatibility `>=5.4,<6.0`, matching the CI-configured and documented
  v5.4 line.
- packaging excludes for examples, tests, docs, tools, PlatformIO metadata, and
  transient build logs.

The `idf` manifest dependency uses ESP-IDF component-manager shorthand syntax
for IDF version compatibility.

## Pure ESP-IDF Example

Added `examples/esp_idf/basic/`:

- `CMakeLists.txt`
- `main/CMakeLists.txt`
- `main/main.cpp`
- `sdkconfig.defaults`
- `README.md`

The example is a minimal diagnostic `app_main()` project. It owns native
ESP-IDF I2C setup, creates a master bus/device using `driver/i2c_master.h`, and
adapts transactions to the framework-neutral `Config` callbacks:

- `Config::i2cWrite`
- `Config::i2cWriteRead`
- `Config::i2cUser`
- `Config::nowMs`
- `Config::i2cLock`
- `Config::i2cUnlock`
- `Config::lockUser`

The example uses a recursive FreeRTOS mutex for both low-level transport access
and the compound input-read/errata lock hooks. This is example-only glue; the
library core remains free of FreeRTOS and ESP-IDF dependencies.

Default diagnostic assumptions:

- SDA: GPIO 8
- SCL: GPIO 9
- I2C port: `I2C_NUM_0`
- Bus speed: 400 kHz
- PCA9555 address: `0x20`
- Safe-output demo disabled by default through `DEMO_CONFIGURE_P00_OUTPUT`

The README states that the example is not a production shared-bus manager and is
not hardware validation by itself.

## ESP-IDF Error Mapping

`esp_err_t` remains confined to the example adapter. Public core APIs still
return `PCA9555::Status`.

Example mapping:

- `ESP_OK` -> `Err::OK`
- `ESP_ERR_TIMEOUT` -> `Err::I2C_TIMEOUT`
- `ESP_ERR_INVALID_ARG` / `ESP_ERR_INVALID_STATE` -> `Err::INVALID_CONFIG`
- callback address mismatch -> `Err::I2C_NACK_ADDR`
- `ESP_ERR_NOT_FOUND` -> `Err::I2C_NACK_ADDR` for address-only probe context,
  otherwise `Err::I2C_BUS`
- `ESP_ERR_INVALID_RESPONSE` -> `Err::I2C_ERROR` with NACK phase marked unknown
- `ESP_FAIL` -> `Err::I2C_NACK_ADDR` only for address-only probe context,
  otherwise `Err::I2C_ERROR`
- fallback IDF errors -> `Err::I2C_BUS`

`Status::detail` preserves the numeric `esp_err_t` value. The example does not
claim data-NACK discrimination when ESP-IDF does not expose the failed NACK
phase.

## Build Reproducibility

PlatformIO reproducibility changes:

- `platformio.ini` pins `platform = espressif32@54.3.20`.
- CI installs `platformio==6.1.19`.
- `platformio.ini` documents the Prompt 06 baseline:
  PlatformIO Core 6.1.19, Espressif 32 platform 54.3.20, Arduino core 3.2.0,
  and ESP-IDF libraries 5.4.0.
- ESP32-S2 build target remains `esp32-s2-saola-1`.
- ESP32-S3 build target remains `esp32-s3-devkitc-1`.
- ESP32-S3 no longer declares PSRAM-specific flags; both embedded targets are
  documented as compile-only 4 MB flash-compatible configurations.

CI changes:

- Existing PlatformIO jobs run on `ubuntu-24.04`.
- Native CI now runs `tools/check_idf_example_contract.py`.
- Added `idf-example-build` matrix for `esp32s2` and `esp32s3` using
  `espressif/esp-idf-ci-action@v1` with `esp_idf_version: v5.4` and
  `path: examples/esp_idf/basic`.

`library.json` now lists both `arduino` and `espidf` frameworks and uses the same
framework-neutral description as `idf_component.yml`.

## Guard Added

Added `tools/check_idf_example_contract.py`.

The guard checks:

- required ESP-IDF component/example files exist,
- root component compiles `src/PCA9555.cpp` and exposes `include/`,
- component metadata does not contain Arduino or `Wire`,
- `idf_component.yml` version matches `library.json`,
- ESP-IDF range is constrained to `>=5.4,<6.0`,
- example consumes the repository as an external component,
- example uses native ESP-IDF I2C APIs,
- callbacks, timeout propagation, lock hooks, and safe-output API usage are
  present,
- README documents timeout behavior, Arduino/Wire absence, non-production scope,
  and no hardware-validation claim.

## Documentation Updated

- `README.md` documents the pure ESP-IDF example and the build matrix.
- `README.md` clarifies that the ESP-IDF example does not claim production
  shared-bus completeness or hardware validation.
- `README.md` records the PlatformIO and ESP-IDF v5.4 build assumptions.
- `examples/esp_idf/basic/README.md` documents the example wiring defaults,
  callback responsibilities, timeout propagation, lock hooks, and IDF v5.4 line.

## Validation Results

| Command | Result |
|---|---|
| `python tools/check_core_timing_guard.py` | PASS: `Core framework guard PASSED` |
| `python tools/check_idf_example_contract.py` | PASS: `ESP-IDF example contract PASSED` |
| `python scripts/generate_version.py check` | PASS: `Version.h` up to date |
| `python tools/check_cli_contract.py` | PASS: `CLI contract PASSED` |
| `python -m platformio test -e native` | PASS: 126 tests, 126 succeeded, duration `00:00:46.708` |
| `python -m platformio run -e esp32s2dev` | PASS: `SUCCESS`, duration `00:00:30.755` |
| `python -m platformio run -e esp32s3dev` | PASS: `SUCCESS`, duration `00:00:24.770` |
| `python -m platformio pkg pack` | PASS: wrote `PCA9555-1.1.0.tar.gz`; generated tarball removed after validation |
| `git diff --check` | PASS; only Git LF-to-CRLF warnings were printed |

## Commands Not Run Or Not Available

| Command | Result |
|---|---|
| `idf.py --version` | FAIL: `idf.py` is not recognized in this local shell |
| `idf.py -C examples/esp_idf/basic set-target esp32s3 build` | NOT RUN locally because `idf.py` is unavailable |
| `idf.py -C examples/esp_idf/basic set-target esp32s2 build` | NOT RUN locally because `idf.py` is unavailable |

Pure ESP-IDF local build validation is therefore not claimed. CI is configured
to build the pure ESP-IDF example for ESP32-S2 and ESP32-S3 with ESP-IDF v5.4.

## Subagent Findings

- `idf-component-agent` recommended a minimal root ESP-IDF component that
  compiles only the framework-neutral core and avoids Arduino/driver dependencies.
- `idf-example-agent` recommended the modern native `driver/i2c_master.h`
  example shape with app-owned bus/device handles and external adapter context.
- `idf-error-mapping-agent` recommended keeping `esp_err_t` inside the example
  and avoiding unsupported data-NACK discrimination.
- `build-repro-agent` identified unpinned PlatformIO inputs and the ESP32-S3
  PSRAM ambiguity; both were corrected.
- `integration-review-agent` identified an over-broad ESP-IDF support range,
  duplicate version drift risk, and wording that could imply hardware validation.
  Metadata, docs, and the guard were updated before final validation.

## Remaining Work

- Prompt 07: documentation and release gates for hardware-validation evidence.
- Prompt 08: final integration, merge-readiness assessment, and release
  packaging guidance.
- A local pure ESP-IDF build still needs an installed ESP-IDF environment or CI
  execution evidence.
- The earlier copy/move deletion behavior remains a release-versioning decision:
  either keep it as a breaking change with the appropriate SemVer release plan,
  or restore compatibility before release packaging.

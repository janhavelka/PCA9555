# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- ESP-IDF component metadata and a native ESP-IDF `examples/espidf_basic` CLI example.
- ESP-IDF port notes in `docs/espidf.md`.
- Dirty-state diagnostics: `hardwareStateDirty()`, `hardwareStateDirtyError()`, and dirty-state fields in `SettingsSnapshot`.
- Glitch-safe output enabling APIs: `preloadOutput()`, `preloadOutputs()`, `setDirection()`, and `configureOutputs()`.
- Chunked I2C job API: `startReadInputsJob()`, `startWriteOutputsJob()`, `startConfigureOutputsJob()`, `pollJob()`, `jobActive()`, `lastJobStatus()`, and `getLastReadInputs()`.
- Interrupt service APIs: `readInputsAndClearInterrupt()`, `clearInterrupts()`, `applyInterruptErrataWorkaround()`, and `applyInterruptErrataWorkaroundUnlocked()`.
- Optional shared-bus lock hooks: `Config::i2cLock`, `Config::i2cUnlock`, and `Config::lockUser`.
- Native fault-injection coverage for register writes, address handling, dirty state, interrupt/errata paths, chunked job budgets, and transport errors.
- Consolidated release checklist in `docs/release.md`.
- Consolidated hardware validation matrix, HIL runbook, and operator template in `docs/hardware_validation.md`.

### Changed

- Core health timestamps now come only from injected `Config::nowMs`; framework time sources live in examples/application glue.
- `tick(nowMs)` now advances one active chunked job instruction instead of being reserved/no-op.
- `applyInterruptErrataWorkaround()` now uses optional lock hooks; use `applyInterruptErrataWorkaroundUnlocked()` for the explicit one-transfer primitive.
- `library.json` now declares both `arduino` and `espidf` framework support.
- Removed the redundant explicit `Wire` dependency from `platformio.ini`; Arduino examples still use the framework-provided Wire library.
- Doxygen input now covers the consolidated docs folder, Arduino CLI source, and native IDF entry point.
- `tools/check_idf_example_contract.py` now validates the native ESP-IDF boundary, command surface, and required CMake dependencies.
- ESP-IDF CLI parity is checked through repo-local command contracts; hardware validation remains pending until target hardware is available.
- Native ESP-IDF mutating CLI commands now require explicit `confirm` suffixes and print a concrete preview plus confirmed command form when omitted.
- Core library is framework-neutral and uses injected timing/I2C ownership only.
- `PCA9555::PCA9555` instances are now explicitly non-copyable and non-movable;
  keep driver instances in stable storage and pass them by reference or pointer.
- Public documentation now distinguishes output latch state, input-register sense, configuration direction, and physical pin behavior.
- Release wording is scoped to production-oriented hardening until the hardware validation matrix is executed.

### Release Status

- Versioning is deferred for this merge branch. `library.json` and `idf_component.yml` still declare `1.1.0`.
- Copy/move deletion is source-compatibility significant and must be considered before tagging a release.
- Pure ESP-IDF local builds still require `idf.py` evidence.
- Real hardware validation has not been run in this branch.

## [1.1.0] - 2026-05-17

### Added

- `driverState()` and status-returning `getSettings(SettingsSnapshot&)` for cross-library diagnostics.
- Native coverage proving latched `OFFLINE` blocks normal I2C operations without touching the bus while `recover()` remains the explicit recovery path.
- CLI version reporting with library version, encoded version, build metadata, and git metadata.

### Changed

- Bring-up CLI output now distinguishes linear API pin numbers (`0-15`) from physical PCA9555 pin labels (`P00-P07`, `P10-P17`) in `pininfo`, `pins`, `sweep`, and `walk`.
- CLI output now uses "output latch" wording for latch operations so input-mode pins are not confused with actively driven outputs.
- CLI input readback now uses "input sense" wording because polarity inversion can intentionally flip the reported input-register bit.
- `selftest` labels now use `PORT_0` / `PORT_1` for port APIs and include physical labels for pin API examples.
- `stress_mix` now prints a start banner, and stress progress keeps color only on `ok=` / `fail=` result counts.
- Async `stress` command processing now waits until the active stress run finishes before consuming queued serial commands or printing the next prompt.
- Doxyfile inputs now cover the root implementation manual and docs tree.
- Reference documentation now keeps durable PCA9555 register notes in `docs/register_reference.md` instead of generated PDF extraction dumps.
- Explicit recovery bypass internals now use the shared `ScopedOfflineI2cAllowance` / `_reassertOfflineLatch()` procedure so failed recovery attempts that begin from `OFFLINE` keep the latch asserted.
- Documented cache-safe output, direction, and polarity write semantics.
- Public Doxygen comments now consistently distinguish output latch state, input-register sense, and physical pin behavior.
- Bulk register helper docs now match PCA9555 auto-increment behavior: 1-2 byte transfers stay within a register pair and odd starts wrap to the pair mate.
- Failed `begin()` clears stale runtime state, and `end()` skips safe-state bus writes when the driver is already `OFFLINE`.
- Health behavior is now standardized on latched `OFFLINE`: normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch I2C until `recover()` succeeds.

### Fixed

- Fixed misleading CLI pin labels that printed linear pins 8-15 as `P08-P15`; physical labels now print as `P10-P17`.
- Fixed README Config Fields table rendering by moving explanatory text out of the table body.
- Removed duplicate Doxygen parameter sections from readback helper comments.
- Guarded raw tracked read helpers against zero-length reads before transport dispatch.
- Treated transport `IN_PROGRESS` statuses as neutral for health counters instead of counting them as failures.
- Added native coverage proving failed output/config/polarity writes do not mutate cached runtime state and invalid register blocks do not touch the bus.

## [1.0.0] - 2026-04-06

### Added
- Initial stable library release of the PCA9555 16-bit I/O expander driver for ESP32-S2 / ESP32-S3 on Arduino / PlatformIO.
- Managed synchronous driver lifecycle with health states (`UNINIT`, `READY`, `DEGRADED`, `OFFLINE`) and tracked transport wrappers.
- Injected I2C transport callbacks so library code never owns or touches `Wire` directly.
- Full 16-bit I/O API for input reads, output writes, direction control, polarity inversion, and direct register access across both ports.
- Single-pin and single-port readback helpers: `readOutput()`, `readOutputPin()`, `getPortConfiguration()`, `getPortPolarity()`, `getPinDirection()`, and `getPinPolarity()`.
- `setPinPolarity()` for cached single-pin polarity updates without exposing raw register operations to applications.
- `SettingsSnapshot` and `getSettings()` for combined runtime configuration and health inspection.
- Pair-bounded bulk register helpers: `readRegisters()` and `writeRegisters()`.
- 16-bit bit-manipulation API: `setOutputBits()`, `clearOutputBits()`, `toggleOutputBits()`, `togglePin()`, `configureInputBits()`, `configureOutputBits()`, `setInvertBits()`, and `clearInvertBits()`.
- `Config::requireConfigPortDefaults` so `begin()` can either enforce POR-default configuration registers or accept a live device state after MCU-only reset.
- Example helper layout under `examples/common/`, including `CliShell.h`, `HealthView.h`, `HealthDiag.h`, and `TransportAdapter.h`.
- Register reference documentation in [docs/register_reference.md](docs/register_reference.md).
- Repository validation scripts: `tools/check_cli_contract.py` and `tools/check_core_timing_guard.py`.
- Auto-generated [include/PCA9555/Version.h](include/PCA9555/Version.h) from `library.json`.
- Native Unity tests covering lifecycle validation, health tracking, transport error mapping, direct register helpers, recovery behavior, and bit-manipulation APIs.

### Changed
- Standardized the bring-up CLI around both terse and descriptive aliases such as `read inputs`, `write pin`, `read reg`, and `cfg/settings`.
- Expanded the bring-up CLI with bulk register commands (`read regs` / `rregs`, `write regs` / `wregs`), single-pin inspection commands (`rout`, `rdir`, `rpol`, `pininfo`), port-specific readback, and full `pins` summaries.
- Added a help glossary that explicitly defines `port`, `pin`, `polarity`, and 16-bit mask `M`, including concrete PCA9555 bit mapping examples.
- Added the `pattern` bring-up command for driving an exact 16-bit output image while forcing all pins to output mode.
- Extended the example `selftest` and `stress_mix` flows so they now exercise mask-based output, direction, and polarity operations in addition to readback and direct register APIs.
- `cmdTogglePin` now uses the driver-level `togglePin()` helper, reducing the command to one I2C write instead of a read-modify-write sequence in the example.
- `cmdAllHigh` and `cmdAllLow` now use dual-port bulk operations instead of multiple per-port writes.
- README documentation now clarifies the example-helper boundary, adds shared PCA9555 terminology, and documents the richer CLI bring-up flow.

### Fixed
- `recover()` now reapplies the latest runtime output, polarity, and direction state instead of reverting to the original `begin()` snapshot.
- Direct register reads and writes now resynchronize cached runtime state so later single-pin helpers remain coherent.
- Public APIs reject invalid `Port` enum values instead of silently treating them as Port 1.
- Overlong CLI input lines are discarded instead of executing truncated commands.
- Example helper parsers reject malformed numeric input and zero-length destination buffers instead of coercing invalid values.

[Unreleased]: https://github.com/janhavelka/PCA9555/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/janhavelka/PCA9555/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/janhavelka/PCA9555/releases/tag/v1.0.0

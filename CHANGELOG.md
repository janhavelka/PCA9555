# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-04-06

### Added
- Initial production release of the PCA9555 16-bit I/O expander driver for ESP32-S2 / ESP32-S3 on Arduino / PlatformIO.
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

[Unreleased]: https://github.com/janhavelka/PCA9555/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/janhavelka/PCA9555/releases/tag/v1.0.0

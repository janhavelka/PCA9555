# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `setPinPolarity()` for single-pin polarity inversion with the same cached read-modify-write ergonomics as `writePin()` and `setPinDirection()`.
- Symmetric single-pin and single-port readback helpers: `readOutput()`, `readOutputPin()`, `getPortConfiguration()`, `getPortPolarity()`, `getPinDirection()`, and `getPinPolarity()`.
- `SettingsSnapshot` and `getSettings()` for a combined runtime configuration plus health snapshot.
- Pair-bounded bulk register helpers: `readRegisters()` and `writeRegisters()`.
- Standardized example helpers: `CliShell.h`, `HealthView.h`, and `TransportAdapter.h`.
- `HealthDiag.h` and `docs/register_reference.md` for example and register-reference parity.
- Repo validation scripts: `tools/check_cli_contract.py` and `tools/check_core_timing_guard.py`.
- `Config::requireConfigPortDefaults` to make the strict POR-default check in `begin()` explicit and configurable.

### Added
- Bit Manipulation API: `setOutputBits()`, `clearOutputBits()`, `toggleOutputBits()`, `togglePin()` for masked output changes in a single 2-byte I2C burst.
- Direction bit manipulation: `configureInputBits()`, `configureOutputBits()` for bulk pin direction changes.
- Polarity bit manipulation: `setInvertBits()`, `clearInvertBits()` for bulk polarity inversion changes.
- CLI commands for bit manipulation: `setbits`/`sb`, `clearbits`/`cb`, `togglebits`/`tb`, `dirin`, `dirout`, `invertset`, `invertclr`.
- `parseU16Token()` CLI parser for 16-bit mask arguments.
- Native unit tests for all bit manipulation methods including no-op optimization and pre-begin rejection.

### Changed
- `cmdTogglePin` now uses library-level `togglePin()` (1 I2C write) instead of `readOutputPin()` + `writePin()` (2 I2C transactions).
- `cmdAllHigh` / `cmdAllLow` now use dual-port bulk operations (2 I2C transactions) instead of 5 separate port-level calls.
- Standardized the bringup CLI with descriptive aliases such as `read inputs`, `write pin`, `read reg`, and `cfg/settings`, while preserving the existing short commands.
- Extended the bringup CLI with bulk register commands (`read regs` / `rregs`, `write regs` / `wregs`) and snapshot-backed health output.
- Expanded the CLI with single-pin inspection/readback commands (`rout`, `rdir`, `rpol`, `pininfo`), full `pins` summaries, per-port semantic readback commands, and stricter numeric parsing for interactive safety.
- Expanded README coverage for runtime recovery behavior, helper layout, and validation commands.

### Fixed
- `recover()` now reapplies the latest runtime output, polarity, and direction state instead of reverting to the original `begin()` configuration.
- Direct register reads/writes now synchronize the driver's cached state so later single-pin operations stay coherent.
- Public APIs now reject invalid `Port` enum values instead of silently treating them as Port 1.
- Overlong CLI input lines are now discarded instead of executing truncated commands.
- Example helper parsers now reject malformed numeric input and zero-length destination buffers instead of silently coercing values.

## [1.0.0] - 2026-06-04

### Added
- Initial release of PCA9555 16-bit I/O expander driver library.
- Managed synchronous driver with health tracking (UNINIT/READY/DEGRADED/OFFLINE).
- Injected I2C transport via function pointers (no Wire dependency in library code).
- Full 16-bit I/O: two independent 8-bit ports with read/write/configure/polarity APIs.
- Single-pin operations with cached read-modify-write for output and configuration registers.
- Device presence verification in `begin()` via configuration register read.
- Configurable interrupt errata workaround (safe command byte after input reads).
- Configurable I2C address (0x20-0x27), timeout, and offline threshold.
- Auto-increment burst reads/writes within register pairs.
- `probe()` diagnostic (raw I2C, no health tracking) and `recover()` with re-apply config.
- Native test suite (Unity) covering: Status, Config defaults, begin() validation, health tracking, probe/recover, input/output/config operations, transport error mapping.
- Example CLI application (`01_basic_bringup_cli`) with I2C scanner, health diagnostics, and interactive pin control.
- Example common helpers: Log.h, BoardConfig.h, I2cTransport.h, I2cScanner.h, CommandHandler.h, BusDiag.h.
- Auto-generated Version.h from library.json via PlatformIO extra_scripts.
- AGENTS.md engineering guidelines.
- README.md with full API reference, behavioral contracts, and errata documentation.

[Unreleased]: https://github.com/janhavelka/PCA9555/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/janhavelka/PCA9555/releases/tag/v1.0.0

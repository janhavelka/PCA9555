# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

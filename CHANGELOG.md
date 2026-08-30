# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- A successful interrupt-errata pointer park no longer counts as a tracked
  health success. Previously an input read that failed and was followed by a
  successful park left the driver reporting `READY` with zero consecutive
  failures, hiding every `readInputs()` failure from a caller's health policy. A
  failed park still records a failure. `totalSuccess` therefore no longer counts
  park transfers.
- Re-establishing a protocol-shadow pair now clears that pair's stale
  `ObservedState::mismatchPairs` bit, so a snapshot can no longer report the same
  pair as both shadow-valid and mismatched.
- Native ESP-IDF startup, `probe`, and `scan` now use `i2c_master_probe()` for
  exact address presence. Phase-ambiguous data transactions remain conservative
  `BUS_ERROR` or `IO_ERROR` results instead of being misreported as address NACKs.
- The native ESP-IDF `rregs` command labelled the second byte of an odd-start
  pair read with the next register instead of the pair partner the chip actually
  returns.
- Example restore helpers restore saved directions only after the saved output
  latches are established; a failed latch restore instead forces all pins to
  inputs, and a failed saved-direction restore gets one all-input fallback.
  Arduino self-test and both examples' mutating diagnostics now run bounded
  phase-aware cleanup after ambiguous setup failures. Failed initial latch-
  setup writes do not retry a byte-identical saved latch or rewrite untouched
  polarity/direction; cleanup expands only as setup phases complete, avoiding
  wrong-latch output enable and early exits that could leave the fixture driven.
- The native ESP-IDF `pins` command builds its summary from four complete pair
  reads instead of sixteen per-pin queries, cutting roughly 80 transfers to 5 and
  clearing the interrupt once instead of sixteen times.
- Pair-read transport failures now invalidate that pair's observation only
  after an actual callback fails. Bus-silent rejections such as `BUSY` preserve
  the last valid observation, and synchronous and cooperative input reads use
  the same rule.
- Driver-wide uncertainty evidence is now mirrored consistently into every
  `ObservedState`, including partial snapshots and `SettingsSnapshot`.
- `probe()` now uses a single raw command-byte write, so Arduino transports can
  observe an address NACK instead of staging an uncompleted repeated-start
  write. Probe remains health-neutral and does not modify a register.
- Arduino write-read result 3 is now treated as a command phase that may have
  reached the device, ensuring the mandatory input-read pointer cleanup is not
  skipped.
- Nonzero cached update requests now reassert their complete register pair and
  report the real transport outcome even when the requested value already
  matches the shadow. Empty masks remain bus-silent no-ops after binding and
  cooperative-ownership guards, without requiring a shadow.
- Empty output/preload requests no longer bypass cooperative ownership checks,
  and the Wire result mapper no longer contains an impossible success branch
  that the core would reject for missing byte counts.
- Native ESP-IDF `verbose` parsing now rejects trailing garbage and reports the
  current mode when no argument is supplied instead of toggling implicitly.
- Native ESP-IDF `read reg` now parses its long form from the correct offset.
  Read-only `stress` no longer requires mutation confirmation, and grouped help
  text marks every mutating mask command explicitly.

### Changed

- Arduino `stress N` is bounded to `1..10000`, matching the native ESP-IDF CLI.
- The native ESP-IDF example configures the selected UART, USB CDC, or USB
  Serial/JTAG console as blocking before running its interactive CLI.
- Repeated register/pair and per-port bookkeeping now uses shared private
  helpers; operation-deadline handling has one entry decision while retaining
  the minimum-time callback-count bound.
- Example pin labels now use the public `portOf()` and `bitOf()` helpers
  directly; the redundant local wrappers were removed.
- Documentation: the README synchronous example now shows a cold start that
  actually works (the previous one returned `SHADOW_INVALID` on its first call);
  the error table lists `CONFIG_REG_MISMATCH` and `UNSUPPORTED`;
  `docs/register_reference.md` is the single owner of register, auto-increment,
  interrupt and errata facts, with `docs/chip_notes.md` reduced to board and
  electrical guidance; `docs/hardware_validation.md` carries an evidence table
  instead of pointing at a dated per-run report.
- Public contracts now describe retained-result discovery after detach,
  whole-operation apply evidence, global uncertainty semantics, cached-update
  reassertion, and the command-write probe behavior. The hardware evidence row
  correctly calls its 22 negative cases bus-silent CLI guard/rejection checks,
  not transport fault injection.

### Removed

- `docs/reports/hil-validation-COM4-pioarduino-55.03.311-20260731.md` and its
  hard-wired references in `library.json`, `tools/check_package.py` and
  `docs/README.md`. Its reviewed evidence is now a row in
  `docs/hardware_validation.md`; the full detail remains in Git history.

## [3.0.2] - 2026-08-07

### Changed

- Added the repository-local Windows PlatformIO wrapper and safe branch-sync
  guidance for contributors and automation agents.
- Consolidated release-status guidance into the README and hardware-validation
  runbook, and kept the generated Doxygen reference focused on current public
  documentation.
- Separated the Arduino and native ESP-IDF contract checkers so each validates
  its own framework surface without circular execution.
- Reduced retained HIL material to the reviewed v3.0.1 summary and durable
  aggregate evidence; transient machine-output locations and the superseded
  COM7 detail report remain available in Git history.

### Removed

- Removed unused example-only `CommandHandler.h`, `TransportAdapter.h`, and
  `HealthView.h` helpers plus the redundant `BusDiag.h` scanner pass-through.

### Fixed

- Corrected the hardware-validation overview to match the v3.0.1 report's final
  2,604 tracked successes and zero failures.

## [3.0.1] - 2026-07-31

### Changed

- Exact-pinned the Arduino example and HIL builds to pioarduino
  `platform-espressif32` `55.03.311` (Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`)
  and added the runtime framework versions to the CLI `version` output.
- Recorded the concise COM7 qualification summary, including a 73-minute,
  15,000,000-operation input/pointer-park stress with zero failures, while
  keeping the remaining physical/analyzer gates explicitly open.
- Condensed normal-mode CLI self-test and stress output to totals plus
  failure/skip detail; verbose mode retains per-check and per-operation detail.
- Expanded the clean-package consumer to exercise a TunnelMonitor-style owner:
  5 ms terminal callbacks, one callback per cooperative poll, exactly-once
  results, complete-image reconciliation, and no hidden retry after a NACK.

### Fixed

- The Arduino `Wire` write-read adapter now treats a zero-byte combined
  transfer failure as an ambiguous command phase and reports no proven device-
  side TX bytes; a partial receive still proves the command was accepted.
- Example I2C initialization now propagates a failed `Wire.begin()` instead of
  reporting unconditional success.
- HIL serial setup now applies DTR/RTS before opening native USB and actively
  frames each new session with a read-only `health` command, avoiding an
  initial asserted-line glitch and passive-prompt startup timeouts.
- HIL aggregate commands now run their declared complete-image recovery before
  a failure limit can stop the session.
- `--timeout-s` is now a minimum and cannot shorten safer command-specific
  bounds.
- Known-command serial responses require their own expected or failure evidence
  before a prompt can terminate them, preventing a delayed USB CDC prompt from
  being attributed to the next command.
- Normal-mode self-test and stress result tails are plain text so ANSI escape
  fragments cannot split machine evidence at a USB packet boundary.
- Arduino and native ESP-IDF CLI raw-write parsing/help now matches the driver:
  `wreg`/`wregs` accept only Output/Polarity starts `2` through `5`, while raw
  Configuration writes remain intentionally rejected.
- `--include-fault-tests` now runs an on-target, bus-silent CLI rejection suite
  and fails unless before/after reported health and settings fields are identical.

### Validation

- Arduino ESP32-S3 and ESP32-S2 builds pass on pioarduino `55.03.311`; 65
  native tests, package-consumer validation, static contracts, HIL parser tests,
  and Doxygen validation pass.
- ESP32-S3 COM4 upgrade HIL passed 46/46 post-fix full/fault results and 72/72
  extended commands. The self-test reported 50/0/0, read stress 1,000/1,000,
  mixed stress 100/100, raw Configuration starts 6/7 were rejected, and the
  final fault block left every reported health/settings field unchanged at
  2,245 successes and zero failures. The subsequent extended plan ended READY
  at 2,604 successes and zero failures. Physical operator/analyzer gates remain
  open in the report.

## [3.0.0] - 2026-07-22

### Added

- Typed `Pin`, `Port`, `Level`, `Direction`, and `PinMask` public contracts and
  data-sheet pin names from `P00` through `P17`.
- `errorName()` for stable static diagnostic text without core logging.
- Caller-owned `RegisterImage` plus explicit `ObservedState` validity,
  mismatch, and uncertainty evidence.
- A single fixed-capacity cooperative operation slot for complete register
  image apply, image verification, and owner-exclusive input read/pointer-park
  service.
- Nonzero request IDs, whole-operation wrap-safe deadlines, per-poll transaction
  budgets, cooperative cancellation/timeout, and exactly-once terminal
  `OperationResult` delivery.
- `TransportResult`, `TransportCode`, and `WriteEffect` so an adapter reports
  byte completion and conservative write-effect evidence for one terminal
  physical attempt.
- Explicit `probe()` and `checkPorDefaults()` diagnostics. POR-default checking
  remains plausibility evidence, not chip identity proof.
- Package-consumer compile validation and a public-source export allowlist.
- A TunnelMonitor-node suitability audit disposition matrix that traces the v2
  findings to v3 library changes and keeps product/hardware findings external.

### Changed

- `bind()` and the compatibility `begin()` alias now validate/store callbacks
  with zero I2C. A failed replacement preserves the existing valid binding.
- `detach()` and `end()` now return `Status` and perform zero I2C.
- Transport callbacks now return one terminal `TransportResult`; callback-level
  retry, bus recovery, and operation-level in-progress results are forbidden.
- Driver state is limited to `UNINIT`, `READY`, and `DEGRADED`. Health counters
  are observational and never gate a requested transfer.
- Apply-image work preloads output latches, writes polarity and direction,
  verifies all writable pairs, reads inputs, and performs the mandatory
  pointer-park workaround in at most eight transport callbacks.
- Input-read cleanup remains bounded and observable after success, after a
  failed receive whose command may have reached the chip, and after caller
  cancellation or whole-operation timeout. Proven not-attempted commands do not
  schedule unnecessary cleanup, and the original read failure remains primary.
- Cached intent, observed hardware state, shadow validity, and uncertain write
  effects are separate. Cached read-modify-write helpers fail when the required
  shadow is invalid or uncertain.
- Raw Configuration-register writes are rejected because they bypass safe
  output-latch preload. Other raw writes invalidate their affected whole-pair
  shadow until a complete pair write re-establishes it.
- The Arduino and native ESP-IDF examples use terminal transport adapters,
  passive binding plus explicit probing, typed pins, and explicit application-
  owned recovery images.
- `library.json` is the version source of truth for generated `Version.h`,
  `idf_component.yml`, and Doxygen `PROJECT_NUMBER`.
- Completed per-symbol Doxygen descriptions for the public transport, status,
  state, lifecycle, cooperative-operation, register, and health contracts.
- Narrowed generated API documentation to public headers and durable project
  guides; undocumented public symbols and documentation errors now fail the
  Doxygen gate instead of being hidden by `EXTRACT_ALL`.
- Kept the root README as the single Doxygen main page. The documentation index
  remains in the repository and package but is excluded from Doxygen input for
  compatibility with the Ubuntu Doxygen 1.9.8 CI runner. Graphviz output is
  explicitly disabled so host-specific Doxygen defaults cannot change the
  documentation build or require an undeclared tool.
- Added a concrete status/error guide, input-data-versus-pointer-cleanup rule,
  generated-documentation workflow, and clearer document ownership to README
  and the documentation index.
- Updated contribution and release guidance so Doxygen must complete without
  warnings and generated HTML remains an ignored local artifact.
- Removed the superseded v2 refactor plan from the live TunnelMonitor
  suitability record, clarified that the retained June HIL summary is
  historical v2 evidence, and changed generic board-review checkboxes so they
  are not mistaken for unfinished library work.
- Reduced live planning documentation to current hardware-validation and
  external-integration work; completed v3 release and audit evidence remains in
  Git history.
- Condensed HIL output to reviewed Markdown and machine-readable JSON summaries
  instead of aggregate and per-command CLI transcripts.
- Moved the AI-coder datasheet extraction under `docs/` and normalized
  `CODEOWNERS` under `.github/`.

### Removed

- Config-owned I2C locks, presence/default checks, offline thresholds, and
  desired-state fields.
- Library-owned retry, bus recovery, and latched offline admission policy.
- Implicit lifecycle I2C and implicit recovery-state selection.
- The v2 chunk-job scheduler, `tick()`, `recover()`, last-job/input accessors,
  and duplicate unlocked errata alias. Use the exact-ID cooperative operation
  API, direct synchronous primitives, returned input data, and caller-owned
  recovery policy. These removals avoid cross-model result consumption.
- `isOnline()`, whose old name could no longer distinguish a passive binding
  from proven device presence. Use `isBound()` and explicit `probe()`/health
  policy.
- The broad v2 `hardwareStateDirty()` and `hardwareStateDirtyError` spellings.
  Use `uncertainPairs()` for ambiguous write effects and `shadowValidPairs()`
  for the separate read-modify-write validity fence.
- The historical v2 HIL report and completed TunnelMonitor audit after retaining
  the still-actionable serial-channel constraint in the hardware guide. The
  comprehensive datasheet extraction remains as intentional AI-coder context.

## [2.0.0] - 2026-06-25

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
- Arduino CLI mutating commands now require the same explicit `confirm` suffixes as the native ESP-IDF CLI.
- Arduino example Wire transport now applies the requested driver I2C timeout for each transaction and restores the previous Wire timeout afterwards.
- Arduino CLI firmware now bounds USB CDC transmit waits and flushes HIL status/prompt boundaries.
- Arduino CLI health output is now compact plain text to avoid ANSI escape fragments and reduce long-HIL serial traffic.
- HIL runner serial DTR/RTS line states are now explicit and recorded in summaries.
- HIL runner prompt capture now uses bounded reads instead of depending only on `in_waiting`, and recognizes prompts only at line starts.
- HIL runner aggregate phases are skipped when setup commands already have serial anomalies.
- HIL runner can now close and reopen the serial port at prompt-safe aggregate command boundaries with `--serial-reopen-interval-s`.
- Core library is framework-neutral and uses injected timing/I2C ownership only.
- `PCA9555::PCA9555` instances are now explicitly non-copyable and non-movable;
  keep driver instances in stable storage and pass them by reference or pointer.
- Public documentation now distinguishes output latch state, input-register sense, configuration direction, and physical pin behavior.
- Release wording is scoped to production-oriented hardening until the hardware validation matrix is executed.
- Transient prompt files and detailed one-off HIL runner reports were removed;
  the durable COM5 constraint is summarized in `docs/hardware_validation.md`.

### Fixed

- Dirty-state readback APIs no longer overwrite the cached desired output, configuration, or polarity state that `recover()` must reapply.
- Dirty-state zero-mask no-op paths now report `BUSY` with `"Hardware state dirty; call recover()"` instead of false success.
- HIL aggregate summaries no longer classify review-captured partial serial output as `PASS`, and a final `FAIL` verdict now returns a nonzero process exit code.

### Release Status

- Version finalized as `2.0.0`; `library.json`, `idf_component.yml`, and generated `Version.h` are synchronized.
- Copy/move deletion is source-compatibility significant and is treated as a major release.
- Pure ESP-IDF local builds still require `idf.py` evidence.
- Continuous COM5 HIL remains blocked by the ESP32-S3 native USB CDC serial path; do not claim production-grade or field-validated status from this release.

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

[Unreleased]: https://github.com/janhavelka/PCA9555/compare/v3.0.2...HEAD
[3.0.2]: https://github.com/janhavelka/PCA9555/compare/v3.0.1...v3.0.2
[3.0.1]: https://github.com/janhavelka/PCA9555/compare/v3.0.0...v3.0.1
[3.0.0]: https://github.com/janhavelka/PCA9555/compare/v2.0.0...v3.0.0
[2.0.0]: https://github.com/janhavelka/PCA9555/compare/v1.1.0...v2.0.0
[1.1.0]: https://github.com/janhavelka/PCA9555/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/janhavelka/PCA9555/releases/tag/v1.0.0

# PCA9555 Industry-Readiness Audit

Date: 2026-05-31
Branch: `audit/pca9555-industry-readiness`
Audit mode: exploration/report-only

> Historical baseline: this audit records the repository state before the
> hardening prompt series. Some findings are intentionally stale after Prompts
> 01-07; use the prompt reports, release checklist, and hardware validation
> matrix for current readiness status.

## Executive Summary

The repository is a solid Arduino/PlatformIO engineering driver, not yet an industry-grade PCA9555 library. The core API understands the important PCA9555 register model, keeps I2C ownership outside the driver, uses `Status` returns instead of silent failure, and implements the PCA9555 interrupt errata workaround by default.

The main blockers are production hardening gaps rather than missing basic functionality: core implementation still depends on Arduino, cache-vs-hardware dirty state is not represented after partial writes or external mutation, runtime output-direction transitions are not guaranteed glitch-safe, the interrupt workaround is not atomic on a shared multi-task bus, copy/move operations are implicitly allowed, and the test harness does not model the most important fault cases.

## Readiness Classification

Engineering-grade with major gaps

This code is suitable for continued hardening and controlled bring-up, but it should not be marketed as production/industry-grade until the P0 findings below are fixed and validated with fault injection and real hardware.

## Scope Reviewed

- `include/PCA9555/CommandTable.h`
- `include/PCA9555/Config.h`
- `include/PCA9555/PCA9555.h`
- `include/PCA9555/Status.h`
- `src/PCA9555.cpp`
- `examples/01_basic_bringup_cli/main.cpp`
- `examples/common/*.h`
- `test/test_basic.cpp`
- `test/stubs/Arduino.h`
- `test/stubs/Wire.h`
- `platformio.ini`
- `library.json`
- `.github/workflows/ci.yml`
- `README.md`
- `CHANGELOG.md`
- `docs/extracted-md/*.md`
- `docs/pdf-extracted-md/PCA9555-Remote-16-bit-I2C-SMBus-IO-Expander-Data-Sheet-SCPS131J.md`
- `docs/register_reference.md`
- `PCA9555_io_expander_implementation_manual.md`

Focused read-only subagents were used for datasheet/protocol, core architecture, API semantics, interrupt/errata, tests/fault injection, and ESP-IDF/Arduino integration. Final integration review was done locally.

## Datasheet Sources

- `docs/PCA9555-Remote-16-bit-I2C-SMBus-IO-Expander-Data-Sheet-SCPS131J.pdf`, TI PCA9555 Rev. J, SCPS131J, March 2021.
- `docs/pdf-extracted-md/PCA9555-Remote-16-bit-I2C-SMBus-IO-Expander-Data-Sheet-SCPS131J.md`
  - Address byte and `0x20..0x27`: local extract around lines 1223 and 1134-1144.
  - Register map/defaults: local extract around lines 1248-1265.
  - Pair auto-increment and wrap: local extract around lines 1313 and 1369.
  - INT behavior and errata: local extract around lines 1097-1121.
  - Electrical limits: local extract around lines 296, 304, and 411.
- `docs/extracted-md/01_chip_overview.md`
- `docs/extracted-md/03_electrical_and_timing.md`
- `docs/extracted-md/04_protocol_commands_and_transactions.md`
- `docs/extracted-md/05_register_map.md`
- `docs/extracted-md/06_modes_interrupts_status_and_faults.md`
- `docs/extracted-md/07_initialization_reset_and_operational_notes.md`
- `docs/application_notes/auto_increment_feature.pdf` was treated only as supplemental I2C background; PCA9555 register truth came from the PCA9555 datasheet.

## Scorecard

| Area | Rating | Notes |
| --- | --- | --- |
| Core framework neutrality | Weak | Public headers are clean, but `src/PCA9555.cpp:8` includes `Arduino.h` and `_nowMs()` falls back to `millis()` at `src/PCA9555.cpp:1168`. |
| I2C ownership/injection | Strong | `Config` injects callbacks at `include/PCA9555/Config.h:18` and `:30`; core wrappers pass address/timeout/user through at `src/PCA9555.cpp:939` and `:950`. |
| Status/error model | Good | All fallible APIs return `Status`; transport result enums exist. Some diagnostics collapse detail, for example `probe()` maps any raw read failure to `DEVICE_NOT_FOUND`. |
| Address validation | Good | `0x20..0x27` constants in `CommandTable.h:66`; validation in `src/PCA9555.cpp:17` and `begin()` rejection at `:112`. Tests miss full address matrix. |
| Register map correctness | Strong | Register constants/defaults match datasheet in `CommandTable.h:18-57`; curated docs agree except one stale `docs/register_reference.md` issue. |
| Input/output latch semantics | Good | API docs distinguish input sense and output latch in `PCA9555.h:164`, `:192`, `:205`; implementation follows that model. |
| Direction-change safety | Medium | `begin()` preloads output latches before direction at `src/PCA9555.cpp:1085`, but runtime `setPinDirection()` / `configureOutputBits()` can enable outputs without a forced preload. |
| Interrupt/errata handling | Medium | Errata workaround is implemented by default, but it is a separate transaction after input read and needs bus-level serialization. No dedicated `clearInterrupts()` API. |
| Partial write/cache consistency | Weak | Cache updates only after success, but no dirty state is exposed if hardware partially changed before a transport failure. Tests do not model partial hardware writes. |
| Probe/strict init honesty | Good | `probe()` is documented as presence only and reads config register raw. Default `begin()` checks config defaults, but this is not a true chip ID. |
| Recovery model | Medium | `recover()` reapplies runtime config, but it cannot force POR and has no dirty-state policy. Health counters ignore successful `begin()` I2C. |
| Thread/ISR contract | Weak | README says single-threaded/not thread-safe, but no ISR-safety contract and no bus-lock/compound-transaction contract. |
| Tests/fault injection | Medium | Native suite is broad for normal API behavior, but thin for all addresses, exact errata payloads, partial writes, cache divergence, copy/move, and bus locking. |
| ESP-IDF readiness | Weak | No `CMakeLists.txt`, `idf_component.yml`, native IDF example, or Arduino-free source build. `idf.py` unavailable in this environment. |
| Arduino ESP32-S2/S3 readiness | Medium | PlatformIO envs exist; S2 build passed; S3 first run failed transiently then verbose rerun passed. Build inputs are not pinned. |
| Documentation honesty | Medium | README and examples are mostly honest, but README calls the library "Production-grade"; current audit does not support that claim. |
| Hardware validation | Unknown | No hardware validation log or matrix results found. |

## What Is Strong

- I2C ownership is correctly outside the library. Core code uses injected callbacks and never calls `Wire` directly; `Wire` lives in example helpers.
- The public API distinguishes input-register sense from output latch state. `readInputs()` / `readInput()` read input registers, while `readOutput()` / `readOutputs()` read output latches.
- The register map and address constants are aligned with the TI datasheet: input `0x00/0x01`, output `0x02/0x03`, polarity `0x04/0x05`, configuration `0x06/0x07`, address `0x20..0x27`.
- `begin()` applies output latch values before writing configuration registers, which is the correct startup ordering for glitch reduction.
- The interrupt errata workaround is enabled by default and parks the command pointer at `0x02`, a nonzero command byte.
- Health tracking uses dedicated tracked wrappers and keeps `probe()` raw/no-health, matching the intended architecture.
- Native tests cover lifecycle, many invalid-parameter paths, health transitions, register pair wrap, errata call count, direct register cache synchronization, and example Wire error mapping.

## High-Severity Findings

### H1. No dirty-state model for partial hardware writes

Severity: High

Evidence:
- Multi-register writes use one callback with command plus up to two data bytes in `writeRegs()` at `src/PCA9555.cpp:1006-1021`.
- Cache/config shadows are updated only after callback success, for example `writeOutputs()` at `src/PCA9555.cpp:327-343`, `setConfiguration()` at `:602-617`, and `setPolarity()` at `:690-703`.
- There is no `hardwareStateDirty()` or equivalent field in `SettingsSnapshot` at `include/PCA9555/PCA9555.h:44-55`.
- The fake bus fails before applying data at `test/test_basic.cpp:44-47`, so it does not model "port 0 changed, port 1 failed" behavior.

Impact:
- If a two-byte write partially reaches the PCA9555 before NACK/timeout/bus failure, hardware can be changed while the driver cache remains unchanged.
- Later cached read-modify-write helpers can overwrite unrelated pins or recovery can reapply a state that the driver believes is still current without admitting the hardware divergence.
- Field diagnostics cannot distinguish "transport failed before touching hardware" from "hardware may now be partially changed".

Recommended remediation:
- Add explicit dirty-state tracking: `hardwareStateDirty()`, `hardwareStateDirtyError()`, and snapshot fields.
- Mark dirty when any multi-byte write can have partially changed hardware and the transport reports failure, unless the transport can certify no byte reached the device.
- Add a controlled `syncFromHardware()` / `recover()` policy that clears dirty only after a full successful reread/reapply sequence.
- Preserve the original transport error while also reporting dirty state.

Suggested tests:
- Fake transport mode that applies only first data byte of output/config/polarity pair then returns `I2C_NACK_DATA`, `I2C_TIMEOUT`, or `I2C_BUS`.
- Assert cache remains unchanged, dirty becomes true, and `recover()`/`syncFromHardware()` clears dirty only after all intended registers are successfully reconciled.
- Repeat for even and odd start registers.

### H2. Runtime direction changes are not guaranteed glitch-safe

Severity: High

Evidence:
- Startup `_applyConfig()` writes output latch before direction at `src/PCA9555.cpp:1085-1103`.
- Runtime `setPinDirection()` only changes the configuration register at `src/PCA9555.cpp:816-840`.
- `configureOutputBits()` also only changes configuration bits at `src/PCA9555.cpp:548-561`.
- `writePin()` and mask helpers can skip I2C on cache no-op at `src/PCA9555.cpp:413-418` and `:476-480`.

Impact:
- A caller can switch a pin from input to output while the hardware output latch contains an old/stale value.
- Cached no-op behavior can prevent a caller from forcing a fresh latch preload before enabling output drive.
- This can create visible glitches or electrically unsafe output levels, especially after MCU reset without PCA9555 POR, multi-master access, or any raw register access outside the driver.

Recommended remediation:
- Add explicit safe APIs, for example `preloadOutput(pin, high)`, `setDirection(pin, dir, optionalPreload)`, and `configureOutputs(mask, latchValues)`.
- For input-to-output transitions, force an output-latch write before clearing configuration bits, even if the cache already matches.
- Document output-to-input false-interrupt risk.
- Consider marking cache dirty after reset/recover uncertainty unless hardware has been read back or reapplied.

Suggested tests:
- Logic-level fake sequence test proving latch write precedes config write for every output-enable API.
- Test that force-preload writes even when cache already matches.
- Test output-to-input path documents or surfaces false interrupt risk.

### H3. Interrupt errata workaround is not atomic against other bus users

Severity: High

Evidence:
- `readInputs()` performs the input read, then separately calls `_applyInterruptErrata()` at `src/PCA9555.cpp:257` and `:267`.
- `_applyInterruptErrata()` performs a one-byte write of `cmd::ERRATA_SAFE_CMD` at `src/PCA9555.cpp:1131-1136`.
- Example transport calls `Wire` directly and has no mutex/compound-operation lock hooks at `examples/common/I2cTransport.h:71-79` and `:116-142`.
- Datasheet errata says the bad condition depends on the PCA9555 command pointer being `0x00` and another slave acknowledging a read before the pointer is moved.

Impact:
- In a single Arduino loop the window is small, but in ESP32 FreeRTOS or any shared bus manager another task can read a different I2C slave between the PCA9555 input read and the parking write.
- That can incorrectly deassert INT and lose interrupt information on a shared bus.

Recommended remediation:
- Keep `applyInterruptErrata=true`.
- Document that the transport must serialize `read input -> park pointer` against all other bus traffic.
- Add optional transport lock hooks or a higher-level compound bus-manager callback so the driver can hold the bus lock across both transactions.
- Provide dedicated `readInputsAndClearInterrupt()` / `clearInterrupts()` APIs with clear lock requirements.

Suggested tests:
- Fake shared bus that records PCA9555 pointer state and simulates another-slave read between transactions.
- Tests asserting the exact errata write payload is one byte `cmd::ERRATA_SAFE_CMD`, not only "one extra write happened".
- Tests for `readInput(PORT_0)`, `readInput(PORT_1)`, `readPin()`, `readRegister(0x00/0x01)`, and odd-start input pair reads.

### H4. Core library is not framework-neutral

Severity: High

Evidence:
- `src/PCA9555.cpp:8` includes `<Arduino.h>`.
- `_nowMs()` falls back to `millis()` at `src/PCA9555.cpp:1168-1172`.
- `library.json:33-38` declares Arduino/espressif32 only.
- There is no repository `CMakeLists.txt`, `idf_component.yml`, or native ESP-IDF example.

Impact:
- The code cannot build as a pure C++/ESP-IDF component even though the transport boundary is otherwise portable.
- Production users using ESP-IDF or non-Arduino test harnesses need stubs or source edits.
- The library's framework neutrality claim is weaker than the API suggests.

Recommended remediation:
- Remove unconditional Arduino dependency from core implementation.
- Require `Config::nowMs` or provide a weak/platform hook isolated behind conditional compilation.
- Add pure ESP-IDF component metadata and a native IDF example using `i2c_master`.
- Keep Arduino `Wire` only in `examples/common`.

Suggested tests:
- Native compile without `Arduino.h` stubs.
- ESP-IDF component build for ESP32-S2 and ESP32-S3.
- Guard script that fails if core `include/` or `src/` includes Arduino/Wire/FreeRTOS/ESP-IDF headers except in platform adapters.

## Medium-Severity Findings

### M1. Driver copy/move operations are implicitly allowed

Severity: Medium

Evidence:
- `class PCA9555` begins at `include/PCA9555/PCA9555.h:58` and declares no deleted copy/move constructors or assignment operators.
- The object contains mutable runtime state, transport callbacks, health counters, and shadows at `include/PCA9555/PCA9555.h:470-487`.

Impact:
- Copying an initialized driver duplicates cached state and health counters for the same hardware address and transport context.
- Two copies can perform conflicting cached read-modify-write operations.

Recommended remediation:
- Delete copy constructor, copy assignment, move constructor, and move assignment unless move semantics are explicitly designed.

Suggested tests:
- Add `static_assert(!std::is_copy_constructible<PCA9555::PCA9555>::value)` and corresponding assign/move assertions.

### M2. Cached RMW helpers assume single-owner hardware state

Severity: Medium

Evidence:
- `writePin()` uses `_cachedOutput*` at `src/PCA9555.cpp:405-418`.
- `setOutputBits()` uses cached output at `src/PCA9555.cpp:472-480`.
- `configureOutputBits()` uses cached config at `src/PCA9555.cpp:553-561`.
- Polarity bit helpers use `_config.polarityPort*` at `src/PCA9555.cpp:569-578` and `:586-595`.

Impact:
- If another master, a diagnostic raw write, or a PCA9555 POR changes hardware behind the cache, "preserve unrelated bits" can become false.

Recommended remediation:
- Document single-owner requirement explicitly.
- Add cache freshness/dirty metadata.
- Offer hardware-readback RMW variants or require explicit `syncFromHardware()` after suspected external mutation.

Suggested tests:
- Mutate fake hardware behind the driver's cache, call each RMW helper, and assert either documented stale-cache behavior or corrected synchronized behavior.

### M3. Test suite misses key fault-injection and boundary coverage

Severity: Medium

Evidence:
- Native tests live in one suite at `test/test_basic.cpp:1451`.
- Address rejection covers `0x30` at `test/test_basic.cpp:171`, but not `0x1F`, `0x28`, `0x00`, `0xFF`, or every valid address.
- Errata tests check write count at `test/test_basic.cpp:599`, `:633`, and `:809`, but fake write does not record exact one-byte payload.
- Partial write failures are not modeled.

Impact:
- The highest-risk production cases can regress without tests.
- Some current guarantees are documented but not mechanically enforced.

Recommended remediation:
- Expand fake transport to record transactions, addresses, payloads, command pointer, and partial hardware effects.
- Add full address, fault, dirty-state, exact errata, copy/move, and cache-divergence tests.

Suggested tests:
- Use the test matrix in `docs/PCA9555_HARDENING_BACKLOG.md`.

### M4. Thread and ISR contracts are incomplete

Severity: Medium

Evidence:
- README says "Single-threaded by default. Not thread-safe" at `README.md:239`.
- No `ISR` or interrupt-context contract was found in public headers, core implementation, or README.
- Public APIs perform blocking I2C and mutate state.

Impact:
- Users may call the driver from an ISR or concurrent FreeRTOS tasks and get blocking behavior, bus races, or corrupted cache/health state.

Recommended remediation:
- Document that all I2C APIs are not ISR-safe.
- Define whether const diagnostics are safe without locking.
- Require external serialization or add optional lock hooks for multi-task use.

Suggested tests:
- Documentation/contract guard checking README and Doxygen mention ISR safety.
- If lock hooks are added, fake lock order tests around compound errata sequences.

### M5. Build and release inputs are not pinned

Severity: Medium

Evidence:
- `platformio.ini:6` uses unpinned `platform = espressif32`.
- CI installs latest PlatformIO with `pip install platformio` at `.github/workflows/ci.yml:41-44`.
- S3 board env adds PSRAM flags at `platformio.ini:46-47`, while PlatformIO reports the selected board as "No PSRAM".

Impact:
- Arduino core/toolchain changes can alter build behavior without repo changes.
- Board configuration may not match actual S3 hardware.

Recommended remediation:
- Pin PlatformIO platform/tool versions.
- Document intended ESP32-S3 board variant and PSRAM expectations.
- Add CI artifact/log retention for exact package versions.

Suggested tests:
- CI check that platform/package versions match pinned known-good versions.
- Build both PSRAM and no-PSRAM S3 variants if both are intended.

### M6. Probe/strict init still cannot identify a PCA9555

Severity: Medium

Evidence:
- `probe()` reads Configuration Port 0 through raw path at `src/PCA9555.cpp:202-213`.
- `begin()` can require config defaults at `src/PCA9555.cpp:121-137`.
- PCA9555 has no chip-ID register.

Impact:
- A different device at `0x20..0x27` that ACKs and returns plausible values can be misclassified as a PCA9555.
- Strict default check can reject a real PCA9555 that was configured before MCU reset.

Recommended remediation:
- Keep documentation honest: `probe()` means "address responds", not "identity proven".
- If strict init is added, preserve/restore writable registers and label it unsafe/diagnostic if it writes to hardware.

Suggested tests:
- Fake non-PCA device returning `0xFF` config to show identity is not proven.
- Strict-init preserve/restore tests if implemented.

### M7. API output parameters can be written before final failure status

Severity: Medium

Evidence:
- `readInputs()` writes `data.port0` and `data.port1` before the errata write at `src/PCA9555.cpp:262-267`.
- `readInput()` similarly returns `value` from the read before errata failure can be reported at `src/PCA9555.cpp:288-296`.

Impact:
- Callers that ignore `Status` can consume values from a transaction sequence that ultimately failed during the mandatory errata parking write.

Recommended remediation:
- Document that output values are valid only on final `Status::Ok()`.
- Consider local temporaries and only commit outputs after the errata step succeeds.

Suggested tests:
- Force errata write failure after successful input read and assert output params are unchanged, if the API is tightened.

## Low-Severity Findings

### L1. `docs/register_reference.md` has incorrect input defaults

Severity: Low

Evidence:
- `docs/register_reference.md:9` lists Input Port 0/1 defaults as `0xFF`.
- TI datasheet shows input register defaults as pin-dependent (`xxxx xxxx`); curated `docs/extracted-md/05_register_map.md:5-6` states this correctly.

Impact:
- Users may believe floating/externally driven inputs are guaranteed high after reset.

Recommended remediation:
- Update `docs/register_reference.md` in a later documentation pass.

Suggested tests:
- Documentation grep/check that input defaults are not listed as fixed `0xFF`.

### L2. `getConfig()` documentation says copy but returns a reference

Severity: Low

Evidence:
- Header comment says "Get a copy of the active configuration" at `include/PCA9555/PCA9555.h:118`.
- Signature returns `const Config&` at `include/PCA9555/PCA9555.h:121`.

Impact:
- Minor API documentation mismatch.

Recommended remediation:
- Change wording to "Get a const reference" or remove the API in favor of `getSettings()` snapshot.

Suggested tests:
- Doxygen/documentation lint for exact wording.

### L3. README production wording is ahead of evidence

Severity: Low

Evidence:
- README starts with "Production-grade" at `README.md:3`.
- This audit found unresolved P0/P1 blockers and no hardware validation results.

Impact:
- Users may over-trust the current release for field deployment.

Recommended remediation:
- Change README wording to "engineering-grade" or "production-oriented" until validation is complete.

Suggested tests:
- Release checklist requiring hardware/fault validation before "production-grade" wording.

## PCA9555 Device-Specific Checklist

| Item | Result | Evidence |
| --- | --- | --- |
| Address range `0x20..0x27` | PASS | `CommandTable.h:66-70`, `src/PCA9555.cpp:17-18`, datasheet address table. |
| Reset defaults | PARTIAL | Code constants OK; `docs/register_reference.md` input defaults wrong. |
| Input register semantics | PASS | `PCA9555.h:164-186`, `src/PCA9555.cpp:250-320`. |
| Output latch semantics | PASS | `PCA9555.h:192-230`, `src/PCA9555.cpp:327-460`. |
| Polarity inversion semantics | PASS | `PCA9555.h:323-357`, `src/PCA9555.cpp:690-814`. |
| Configuration/direction semantics | PASS | `PCA9555.h:300-321`, `src/PCA9555.cpp:602-688`. |
| 8-bit port access | PASS | `readInput`, `writeOutput`, `setPortConfiguration`, `setPortPolarity`. |
| 16-bit bulk access | PASS | `PortData`, `writeOutputs`, `readInputs`, pair helpers. |
| Command byte handling | PASS | Explicit command byte per read/write; pair length capped at 2. |
| Interrupt clear semantics | PARTIAL | Input reads clear by hardware behavior; no explicit clear API or port-source abstraction. |
| Interrupt errata workaround | PARTIAL | Implemented by default, but not atomic across shared-bus users. |
| Output preload before direction change | PARTIAL | Correct in `begin()` only; runtime output-enable APIs do not force preload. |
| False interrupt risk on output-to-input transition | PARTIAL | Datasheet risk is known in docs, but API does not surface it. |
| No chip-ID / probe honesty | PASS | `probe()` documented as presence/raw config read, not identity proof. |
| Power-on reset limitations | PASS | README documents no SW reset and MCU-reset caveat. |
| Electrical safety documentation | PARTIAL | Current limits and pullups documented; no board load budget or validation. |

## API Latency / Blocking Model

Default `i2cTimeoutMs` is 50 ms. The driver delegates actual timeout enforcement to the transport; worst-case below assumes each I2C callback blocks until that timeout.

| API | I2C transactions | Other waits | Worst-case bound | Notes |
| --- | ---: | --- | --- | --- |
| `begin(config)` | 6 max | None | `6 * timeout` | Raw config read, output write, polarity write, config write, input read, optional errata write. Stops early on failure. |
| `tick(nowMs)` | 0 | None | O(1) | No-op today. |
| `end()` | 0 or 1 | None | `timeout` | Best-effort raw config write if initialized and not offline. |
| `probe()` | 1 | None | `timeout` | Raw config register read; no health update. |
| `recover()` | 6 max | None | `6 * timeout` | Tracked config read plus `_applyConfig()` sequence. |
| `readInputs()` | 1 or 2 | None | `2 * timeout` | Burst input read plus optional errata write. |
| `readInput()` | 1 or 2 | None | `2 * timeout` | Single input register plus optional errata write. |
| `readPin()` | 1 or 2 | None | `2 * timeout` | Delegates to `readInput()`. |
| `writeOutputs()` | 1 | None | `timeout` | One command plus two data bytes. |
| `writeOutput()` | 1 | None | `timeout` | One command plus one data byte. |
| `writePin()` | 0 or 1 | None | `timeout` | Cache no-op skips I2C; otherwise delegates to `writeOutput()`. |
| `readOutputs()` | 1 | None | `timeout` | Burst output-latch read. |
| `readOutput()` / `readOutputPin()` | 1 | None | `timeout` | Single output-latch read. |
| `setConfiguration()` | 1 | None | `timeout` | One command plus two data bytes. |
| `setPortConfiguration()` | 1 | None | `timeout` | One command plus one data byte. |
| `setPinDirection()` | 0 or 1 | None | `timeout` | Cache no-op skips I2C; no forced preload. |
| `setPolarity()` | 1 | None | `timeout` | One command plus two data bytes. |
| `setPortPolarity()` / `setPinPolarity()` | 0 or 1 | None | `timeout` | Single-pin path can no-op. |
| Mask helpers | 0 or 1 | None | `timeout` | Cached 16-bit RMW then burst write. |
| `readRegisters()` | 1 or 2 | None | `2 * timeout` | Optional errata write when start register is an input register. |
| `writeRegisters()` | 1 | None | `timeout` | Max len 2 enforced by internal helper. |

## Partial-State / Cache Consistency Assessment

The driver is cache-safe only for all-or-nothing transport behavior. It updates output/config/polarity mirrors only after a successful callback. This prevents false cache updates when a callback fails before touching hardware.

The missing case is partial hardware mutation. PCA9555 pair writes are command plus two data bytes; if byte 1 reaches the device and byte 2 fails, hardware and cache diverge. The current driver returns the transport error and leaves cache unchanged, but it does not mark hardware state dirty or expose uncertainty.

Affected operations:
- `writeOutputs()`
- `setConfiguration()`
- `setPolarity()`
- `writeRegisters(startReg, len=2)`
- `setOutputBits()`, `clearOutputBits()`, `toggleOutputBits()`
- `configureInputBits()`, `configureOutputBits()`
- `setInvertBits()`, `clearInvertBits()`
- `_applyConfig()` during `begin()` and `recover()`

Single-byte writes are less exposed to cross-port partial state, but they can still leave cache stale if the transport reports failure after the byte reached the device.

Recommended model:
- Treat any failed write as "hardware may be dirty" unless the transport can explicitly report "no byte accepted".
- Provide diagnostic access to dirty state.
- Provide explicit recovery policy: reread writable registers, reapply desired state, or require application decision.

## Interrupt/Errata Assessment

The implementation handles the basic PCA9555 errata correctly for single-owner use. Input reads are followed by a one-byte command write of `0x02`, which is not `0x00`. That matches the TI workaround for the case where command pointer `0x00` plus another slave read can improperly deassert INT.

The remaining production risk is bus serialization. The driver API exposes `readInputs()` as one public call, but internally this is two I2C transactions: input read, then command-pointer parking write. The transport contract has no lock hooks and the example `Wire` adapter does not serialize against other tasks. A shared ESP32 bus manager must prevent any other I2C read from occurring between those two transactions.

INT clear behavior is also only implicit. Reading one input port clears that port's interrupt source by hardware behavior; reading the other port is separate. `readInputs()` burst-reads both ports and is the safest default for interrupt servicing. `readInput()` and `readPin()` are valid but can leave the other port's interrupt state pending.

Missing public affordances:
- `readInputsAndClearInterrupt()`
- `clearInterrupts()`
- `applyInterruptErrataWorkaround()`
- Documentation that none of these I2C paths are ISR-safe
- Transport lock/compound-operation requirements

## Test Coverage Assessment

Current tests:
- Config defaults and validation.
- Invalid address high-side example (`0x30`).
- Lifecycle, failed begin cleanup, offline latch, recover behavior.
- Example `Wire` error mapping for write path.
- Input/output/config/polarity APIs.
- Register range and pair wrap.
- Errata workaround call-count tests.
- Direct register cache synchronization.
- Bit manipulation helpers and no-op paths.

Missing or weak:
- Full valid address matrix `0x20..0x27` and lower/upper invalid matrix.
- Exact callback address assertion.
- Exact errata payload and command pointer state.
- Partial writes that mutate hardware before failure.
- Dirty-state behavior and recovery/sync behavior.
- Cache divergence after external register mutation.
- Copy/move compile-time prevention.
- ISR/thread-safety contract tests or doc guards.
- Driver propagation of every transport error on read/write/begin/recover/errata paths.
- Native test runner currently errored in this Windows environment with `[WinError 2]`.

## ESP-IDF Port Assessment

There is no pure ESP-IDF component or example in the repository. No `CMakeLists.txt`, `idf_component.yml`, or `examples/esp_idf/basic` exists. `library.json` declares Arduino only, and the core implementation includes Arduino for `millis()`.

ESP-IDF readiness is therefore weak. A real IDF port needs:
- Arduino-free core source.
- IDF component metadata.
- Native IDF transport adapter using `i2c_master`.
- Example with `app_main()`.
- Bus locking and timeout/error mapping using ESP-IDF primitives.

## Hardware Validation Matrix

| Test | Method | Expected result | Current status |
| --- | --- | --- | --- |
| Scan/probe each physically wired address | Wire/IDF scan, then driver `probe()` | Only wired addresses respond | Not run |
| Verify POR defaults | Power-cycle, read writable regs | Output `0xFFFF`, polarity `0x0000`, config `0xFFFF` | Not run |
| Verify input reads all pins | Drive each pin high/low safely | Input register follows state/polarity | Not run |
| Verify output writes all pins | LEDs or safe loads with current limits | Output latch drives configured outputs | Not run |
| Verify output preload before direction | Logic analyzer | Latch write precedes output enable | Not run |
| Verify polarity inversion | Per port and per pin | Input sense inverts only input reads | Not run |
| Verify INT active-low | Pull-up on INT, trigger input edges | INT asserts low on input change | Not run |
| Verify INT clear port 0/1 separately | Trigger one port at a time | Reading same port clears source | Not run |
| Verify both-port INT clear | Trigger both ports | `readInputs()` clears both | Not run |
| Verify false interrupt output->input | Change direction with known levels | Documented false INT observed/handled | Not run |
| Verify errata workaround | Shared bus with another readable slave | No improper INT deassertion with workaround | Not run |
| Verify no-workaround behavior | Disable workaround in controlled test | Reproduce errata or classify setup | Not run |
| Verify NACK/unplug/replug recovery | Disconnect/reconnect device | DEGRADED/OFFLINE/recover match policy | Not run |
| Verify brownout/power-cycle defaults | Power-cycle PCA9555 only | Driver detects/reapplies or reports dirty | Not run |
| Verify 100 kHz and 400 kHz | Bus speed sweep | Stable operations at both rates | Not run |
| Long shared-bus soak | Mixed reads/writes with other slaves | No cache/INT/health anomalies | Not run |

## Recommended Implementation Plan

### P0 - Must fix before production claim

1. Add dirty-state model for all failed writes and partial write uncertainty.
2. Add safe runtime output-direction APIs that force latch preload before enabling outputs.
3. Add transport serialization/lock contract for input-read plus errata parking sequence.
4. Remove Arduino dependency from core or explicitly scope the library as Arduino-only.
5. Delete copy/move operations for the driver.

### P1 - Should fix before release/merge

1. Expand fake transport to record payloads, command pointer, partial writes, and exact address.
2. Add full address matrix and transport error propagation tests.
3. Add ISR/thread-safety documentation and Doxygen comments.
4. Pin PlatformIO/Arduino toolchain versions and clarify S3 PSRAM board variant.
5. Correct `docs/register_reference.md` input defaults.
6. Tighten `getConfig()` documentation or return semantics.

### P2 - Nice later hardening

1. Add pure ESP-IDF component and example.
2. Add hardware validation log template and release gate checklist.
3. Add optional strict diagnostic init with preserve/restore behavior.
4. Add public interrupt helper APIs with clear naming.
5. Add CI artifacts for build package versions and memory usage.

## Suggested Follow-Up Prompt Chunks

1. Core portability and ownership contract: remove Arduino dependency, define time source behavior, delete copy/move, document thread/ISR rules.
2. Register model, cache, dirty state, and recovery: add dirty-state fields/APIs, sync/recover policy, partial-write handling, and diagnostics.
3. Safe output/direction APIs: implement forced preload, output-enable sequencing, false-interrupt documentation, and tests.
4. Interrupt and errata hardening: add clear/read interrupt helpers, exact errata tests, and bus-lock/compound-operation contract.
5. Fault-injection test matrix: expand fake transport for addresses, partial writes, command pointer, short reads/writes, and all transport errors.
6. ESP-IDF/Arduino build hardening: pin toolchains, add IDF component/example, clarify board configs, and update CI.
7. Documentation and release readiness: correct stale docs, remove unsupported "production-grade" claims, and add hardware validation results template.

## Commands Attempted

| Command | Result |
| --- | --- |
| `git rev-parse --show-toplevel` | PASS: `C:/Users/HonzovoSpectre/Documents/Projects/PCA9555` |
| `git branch --show-current` | PASS before branch: `main`; audit branch then created. |
| `git status --short` | PASS: clean before work. |
| file listing to depth 3 | PASS: repo structure captured. |
| local datasheet/doc search | PASS: PCA9555 datasheet PDF and extracted Markdown found. Parent search hit unrelated project paths and access-denied directories. |
| `python --version` | PASS: Python 3.13.12 |
| `python -m platformio --version` | PASS: PlatformIO Core 6.1.19 |
| `python tools/check_core_timing_guard.py` | PASS: Core timing guard PASSED |
| `python tools/check_cli_contract.py` | PASS: CLI contract PASSED |
| `python tools/check_idf_example_contract.py` | UNAVAILABLE: file not found |
| `python scripts/generate_version.py check` | PASS: `Version.h` up to date |
| `python -m platformio test -e native` | ERROR: PlatformIO collected 1 test, then test runner failed with `[WinError 2] The system cannot find the file specified`; no Unity assertions ran. |
| `python -m platformio run -e esp32s3dev` | First run FAILED at `.pio\build\esp32s3dev\FrameworkArduino\esp32-hal-matrix.c.o` with no diagnostic in normal verbosity. |
| `python -m platformio run -e esp32s3dev -v` | PASS on immediate verbose rerun; RAM 6.8%, flash 29.9%. |
| `python -m platformio run -e esp32s2dev` | PASS; RAM 11.2%, flash 29.1%. |
| `python -m platformio pkg pack` | PASS; tarball created and removed to keep audit-only worktree clean. |
| `idf.py --version` | UNAVAILABLE: `idf.py` not recognized. |
| `idf.py -C examples/esp_idf/basic set-target esp32s3 build` | UNAVAILABLE: no `idf.py` and no `examples/esp_idf/basic`. |
| `idf.py -C examples/esp_idf/basic set-target esp32s2 build` | UNAVAILABLE: no `idf.py` and no `examples/esp_idf/basic`. |

## Final Verdict

The repository is ready for focused hardening, not production release. The driver has the right high-level shape and several good PCA9555-specific decisions, but production claims are blocked by dirty-state handling, runtime direction safety, shared-bus interrupt serialization, framework neutrality, copy/move semantics, missing fault-injection coverage, and missing hardware validation.

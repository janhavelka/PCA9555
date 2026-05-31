# PCA9555 Hardening Backlog

Date: 2026-05-31
Branch: `audit/pca9555-industry-readiness`

This backlog turns the audit findings into implementation-focused prompt chunks. Each chunk is intended to be copied into a future coding session. Keep changes small and test-driven.

## Chunk 1 - Core Portability, Driver Ownership, Copy/Move

```text
You are working in the PCA9555 repository. Implement the first P0 hardening chunk only.

Goals:
- Remove unconditional Arduino dependency from core library source.
- Keep public headers Arduino/Wire/ESP-IDF/FreeRTOS-free.
- Preserve Arduino example behavior through example-only adapters.
- Delete PCA9555 driver copy/move operations.
- Document thread and ISR contract in public Doxygen and README.

Constraints:
- Do not edit generated include/PCA9555/Version.h.
- No direct Wire usage in include/ or src/.
- No heap allocation in normal operations.
- All fallible APIs keep returning Status.

Implementation guidance:
- Replace src/PCA9555.cpp unconditional #include <Arduino.h> and millis() fallback with a portable time hook policy.
- Prefer requiring Config::nowMs for health timestamps, or isolate platform fallback behind a small conditional compile section that does not affect pure C++ builds.
- Add deleted copy constructor, copy assignment, move constructor, and move assignment to PCA9555::PCA9555.
- Add static_assert tests in native suite for copy/move disabled.
- Add grep guard coverage so core source cannot include Arduino.h/Wire.h.
- README/Doxygen must say blocking I2C APIs are not ISR-safe and the driver is externally serialized for multi-task use.

Verification:
- python tools/check_core_timing_guard.py
- python scripts/generate_version.py check
- python -m platformio test -e native
- python -m platformio run -e esp32s2dev
- python -m platformio run -e esp32s3dev
```

## Chunk 2 - Dirty-State Model and Partial Write Semantics

```text
Implement dirty-state tracking for PCA9555 hardware/cache divergence.

Goals:
- Add public diagnostics:
  - bool hardwareStateDirty() const
  - Status hardwareStateDirtyError() const
  - dirty fields in SettingsSnapshot
- Mark dirty when a write may have changed hardware but returned failure.
- Keep original transport error visible.
- Clear dirty only after explicit successful reconciliation.

Design requirements:
- Failed config/param validation and NOT_INITIALIZED must not mark dirty.
- Failed reads must not mark dirty unless they occur inside a recovery/reapply sequence after writes.
- Failed single-byte and two-byte writes should conservatively mark dirty unless the transport can prove no byte reached hardware.
- Multi-register pair writes must be treated as partial-state risk.
- recover() must have a clear policy: either reapply desired shadow state and clear dirty after full success, or expose a separate syncFromHardware()/reapplyDesiredState() API.

Test requirements:
- Extend FakeBus to support partial write effects:
  - apply command + first data byte, then return I2C_NACK_DATA
  - apply command + first data byte, then return I2C_TIMEOUT
  - apply a one-byte register write, then return I2C_BUS
- Assert cache is not falsely updated after failed writes.
- Assert dirty is set with the transport error.
- Assert dirty clears only after full successful recover/sync.
- Cover output, configuration, polarity, and direct writeRegisters with even and odd pair starts.
```

## Chunk 3 - Glitch-Safe Runtime Direction APIs

```text
Add production-safe output preload and direction-change APIs.

Goals:
- Provide explicit safe APIs for runtime direction changes:
  - preloadOutput(Pin pin, bool high)
  - setDirection(Pin pin, Direction dir) or equivalent
  - configureOutputs(uint16_t outputMask, uint16_t outputValues) or equivalent bulk helper
- Ensure input-to-output transitions write desired output latch before clearing configuration bit.
- Ensure forced preload can perform I2C even when cache already matches.
- Document output-to-input false interrupt risk.

Constraints:
- Preserve existing APIs if possible, but document any unsafe legacy sequencing.
- Keep no heap allocation and no logging in library code.
- All I2C through tracked wrappers.

Test requirements:
- Fake transaction log proves output latch write precedes config write for pin and mask output-enable paths.
- Force-preload writes even when cache already contains requested bit.
- Failed preload does not change direction.
- Failed direction write after successful preload reports error and sets dirty as appropriate.
- Existing begin() ordering remains output -> polarity -> config -> input read -> errata.
```

## Chunk 4 - Interrupt and Errata Hardening

```text
Harden PCA9555 INT handling and errata support.

Goals:
- Add explicit public APIs:
  - readInputsAndClearInterrupt(uint16_t& value) or PortData equivalent
  - clearInterrupts()
  - applyInterruptErrataWorkaround()
- Document that readInputs() is the safe default for INT service because it reads both ports.
- Document that readInput()/readPin() clear only the port they read.
- Define bus serialization requirement for input-read + pointer-park sequence.
- Optionally add transport lock hooks:
  - lock(user, timeoutMs)
  - unlock(user)
  or an equivalent compound-transaction mechanism.

Implementation requirements:
- Keep Config::applyInterruptErrata default true.
- Errata parking command must remain nonzero, currently cmd::ERRATA_SAFE_CMD == 0x02.
- If lock hooks are added, hold the lock across input read and errata write.
- No ISR-safe claim for APIs that touch I2C.

Test requirements:
- Fake bus records one-byte writes and final command pointer.
- Assert every input read path writes exactly cmd::ERRATA_SAFE_CMD when enabled.
- Assert disabled mode skips only the errata write.
- Assert errata-write failure updates health and does not silently report OK.
- Fake shared-bus test shows another-slave read cannot occur inside locked compound sequence.
```

## Chunk 5 - Fault-Injection Test Matrix

```text
Expand native tests and fake transport coverage without changing driver behavior beyond what is needed for tests.

Goals:
- Full valid/invalid address matrix.
- Full register pair matrix.
- Full transport error propagation matrix.
- Exact callback address and payload assertions.
- Cache divergence tests.
- Copy/move static assertions.

Fake transport additions:
- Record every transaction: type, address, tx bytes, rx bytes, status.
- Track command pointer.
- Simulate pair auto-increment exactly.
- Support short write, short read, unavailable data, NACK address, NACK data, timeout, bus error.
- Support external hardware mutation helpers.

Tests to add:
1. Addresses 0x20..0x27 accepted and passed to callbacks.
2. Addresses 0x1F, 0x28, 0x00, 0xFF rejected without bus access.
3. probe() preserves meaningful NACK/timeout/bus detail or documents mapping.
4. begin() reads/writes only safe registers and does not use input regs for identity.
5. Defaults modeled correctly: config 0xFFFF, output 0xFFFF, polarity 0x0000, input pin-dependent.
6. 8-bit register reads/writes use correct command bytes.
7. 16-bit pair writes order port 0 then port 1.
8. Odd-start pair operations wrap within the pair.
9. Cache is not falsely updated after failed writes.
10. Dirty state tracks partial write uncertainty.
11. Pin write does not affect unrelated pins under fresh cache.
12. Direction change to output preloads first.
13. Direction change to input documents/handles false interrupt risk.
14. Polarity inversion affects input interpretation only.
15. Errata workaround exact payload and error behavior.
16. No public I2C API is ISR-safe.
17. Copy/move disabled.
```

## Chunk 6 - ESP-IDF Component and Build Reproducibility

```text
Add ESP-IDF readiness and build reproducibility hardening.

Goals:
- Add pure ESP-IDF component metadata:
  - CMakeLists.txt
  - idf_component.yml if appropriate
- Add examples/esp_idf/basic with app_main().
- Keep Arduino example intact.
- Pin PlatformIO platform/tool versions or document exact supported versions.
- Clarify ESP32-S3 PSRAM/no-PSRAM board targets.

Implementation guidance:
- Core library must build without Arduino headers.
- IDF example should own bus initialization and locking.
- IDF transport should map esp_err_t to PCA9555::Status without leaking esp_err_t through public API.
- Include timeout behavior and bus lock comments.
- Add a guard script equivalent to tools/check_idf_example_contract.py.

Verification:
- python tools/check_core_timing_guard.py
- python tools/check_cli_contract.py
- python tools/check_idf_example_contract.py
- python -m platformio run -e esp32s2dev
- python -m platformio run -e esp32s3dev
- idf.py -C examples/esp_idf/basic set-target esp32s3 build
- idf.py -C examples/esp_idf/basic set-target esp32s2 build
```

## Chunk 7 - Documentation, Release Gates, and Hardware Validation

```text
Clean up documentation and add release-readiness gates.

Goals:
- Fix docs/register_reference.md input defaults.
- Remove or qualify unsupported "production-grade" claims until validation is complete.
- Add hardware validation matrix and result log template.
- Add release checklist that blocks production claims without passing test/build/hardware gates.
- Update README API notes for dirty state, safe direction APIs, interrupt lock requirements, probe honesty, and ISR/thread contract.

Documentation requirements:
- Input Port defaults are pin-dependent, not fixed 0xFF.
- Output Port defaults are 0xFF latch values.
- Polarity defaults are 0x0000.
- Configuration defaults are 0xFFFF.
- `probe()` means address responds; it is not chip-ID proof.
- `recover()` cannot force true PCA9555 POR.
- INT requires pull-up and input-read clear behavior is port-specific.
- Shared-bus errata workaround requires serialization.

Hardware validation template must include:
- Wired address scan/probe.
- POR defaults.
- Input reads all pins.
- Output writes all pins with safe loads.
- Latch preload before direction change with logic analyzer.
- Polarity inversion.
- INT assert/clear per port and both ports.
- Errata workaround on shared bus.
- NACK/unplug/replug recovery.
- Brownout/power-cycle behavior.
- 100 kHz and 400 kHz operation.
- Long shared-bus soak.
```

## Suggested Execution Order

1. Chunk 1 - Core Portability, Driver Ownership, Copy/Move
2. Chunk 2 - Dirty-State Model and Partial Write Semantics
3. Chunk 3 - Glitch-Safe Runtime Direction APIs
4. Chunk 4 - Interrupt and Errata Hardening
5. Chunk 5 - Fault-Injection Test Matrix
6. Chunk 6 - ESP-IDF Component and Build Reproducibility
7. Chunk 7 - Documentation, Release Gates, and Hardware Validation

Do not update `library.json` or `CHANGELOG.md` until implementation changes are complete and the release scope is known.

# AGENTS.md - PCA9555 Production Embedded Guidelines

## PlatformIO

Before editing, fetch remotes and fast-forward the newest intended working
branch to its upstream. Stop and report dirty, divergent, or conflicted state;
never overwrite work to force a sync.

On Windows, use `.\scripts\pio.cmd <arguments>`; it selects the current user's
VS Code-managed installation. Never install another PlatformIO Core; if the
wrapper cannot find it, stop and report the missing installation.

## Role and Target
You are a professional embedded software engineer building a production-grade PCA9555 16-bit I/O expander library.

- Target: ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO.
- Goals: deterministic behavior, long-term stability, clean API contracts, portability, no surprises in the field.
- These rules are binding.

---

## Repository Model (Single Library)

```
include/PCA9555/         - Public API headers only (Doxygen)
  CommandTable.h         - Register addresses and bit masks
  Status.h
  Config.h
  PCA9555.h
  Version.h              - Auto-generated (do not edit)
src/                     - Implementation (.cpp)
examples/
  01_*/
  common/                - Example-only helpers (Log.h, BoardConfig.h,
                           I2cTransport.h, I2cScanner.h, HealthDiag.h)
platformio.ini
library.json
README.md
CHANGELOG.md
AGENTS.md
```

Rules:
- `examples/common/` is NOT part of the library. It simulates project glue and keeps examples self-contained.
- No board-specific pins/bus in library code; only in `Config`.
- Public headers only in `include/PCA9555/`.
- Examples demonstrate usage and may use `examples/common/BoardConfig.h`.
- Keep the layout boring and predictable.

---

## Core Engineering Rules (Mandatory)

- Prefer simple, clear, correct, robust, safe, readable code over clever abstractions or speculative flexibility.
- Before coding, inspect existing owners/modules for code that can be simplified, reused, or deleted.
- Prefer deleting unnecessary code over adding code.
- Keep changes tightly scoped to the user's request.
- Preserve dirty user changes and never revert unrelated work unless explicitly asked.
- Prefer extending existing owners/modules/APIs/contracts over creating parallel abstractions.
- Before adding a service, class, file, interface, or abstraction, confirm a concrete current need and a clear caller or test.
- Do not add placeholder classes, future stubs, empty managers, broad frameworks, plugin systems, registries, generic layers, or speculative extension points unless the current task explicitly requires them.
- Prefer explicit state, explicit ownership, and small local helpers over hidden global state.
- Deterministic: no unbounded loops, waits, retries, allocations, queues, or buffers in steady paths.
- All multi-transfer operation timeouts use wrap-safe deadlines; never `delay()` in library code.
- Passive lifecycle: `Status bind(const Config&)` and the compatibility
  `begin()` alias validate/store callbacks with zero I/O. `end()` performs zero
  I/O. Presence checks and register changes are explicit operations.
- Single-transfer register operations may complete synchronously. Multi-transfer
  chip work must expose fixed cooperative phases driven by the caller with an
  explicit transaction budget. A resource owner may use a budget of one so one
  owner poll performs at most one transport callback.
- No heap allocation in steady state (no `String`, `std::vector`, `new` in normal ops).
- Avoid dynamic allocation in steady embedded paths unless it is already an accepted local pattern and the bound is clear.
- Every hardware operation that can block must have a timeout and an observable failure path.
- Chip-state verification and reconciliation must be bounded, deterministic,
  explicit, and testable. Bus recovery and retry policy belong to the caller.
- Do not hide hardware failures behind silent retries or fake success.
- No logging in library code; examples may log.
- No macros for constants; use `static constexpr`. Macros only for conditional compile or logging helpers.

---

## I2C Manager + Transport (Required)

- The I2C bus must have one clear owner.
- The library MUST NOT own I2C. It never touches `Wire` directly.
- Device drivers must not directly own or reconfigure a shared bus unless this repository's architecture explicitly says so.
- `Config` MUST accept a transport adapter (function pointers or abstract interface).
- I2C transactions MUST be timeout-bounded and report errors clearly.
- Each callback invocation is exactly one terminal physical attempt. Transport
  callbacks must not return an operation-level in-progress result. The library
  never retries a callback or recovers the bus.
- `WriteEffect` describes the device-side transmit phase. For write-read
  callbacks it must conservatively state whether the command byte was accepted;
  `NOT_ATTEMPTED` is valid only when the adapter can prove it was not.
- Transport errors MUST map to `Status` (no leaking `Wire`, `esp_err_t`, etc.).
- The library MUST NOT configure bus timeouts or pins.
- Do not implement chip protocols manually if an existing hardened project library already provides the needed timeout, recovery, and testability behavior.
- Keep chip-level protocol code inside the driver/wrapper. Keep application policy outside the chip driver.
- Do not add fake devices, simulated buses, or test doubles to production paths.

## Framework Boundaries (Mandatory)

- Core/public headers and `src/` MUST NOT require Arduino or ESP-IDF framework headers.
- Arduino examples may use Arduino APIs.
- ESP-IDF examples MUST be native IDF applications using `app_main`, `driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, and fixed C buffers or native console APIs.
- ESP-IDF examples MUST NOT include Arduino CLI sources or use Arduino compatibility facades such as `ArduinoCompat`, `IdfArduinoCompat`, `Arduino.h`, `Wire.h`, `String`, `Serial`, or `TwoWire`.
- Maintain CLI command parity through repo-local command contracts/checkers, not by sharing Arduino implementation source.

---

## Status / Error Handling (Mandatory)

All fallible APIs return `Status`:

```cpp
struct Status {
  Err code;
  int32_t detail;
  const char* msg;  // static string only
};
```

- Silent failure is unacceptable.
- No exceptions.

---

## PCA9555 Driver Requirements

- I2C address configurable: 0x20–0x27 (3 hardware address pins A0, A1, A2).
- `bind()`/`begin()` perform zero I/O. `probe()` is the explicit address-response
  check. POR-default checking is a separate explicit diagnostic and is not chip
  identity proof.
- 16 I/O pins organized as two 8-bit ports (Port 0: P00–P07, Port 1: P10–P17).
- Each pin independently configurable as input or output.
- 8 internal registers in 4 pairs:
  - Input Port 0/1 (read-only, reflects pin state)
  - Output Port 0/1 (read/write, latched)
  - Polarity Inversion Port 0/1 (read/write)
  - Configuration Port 0/1 (read/write, 1=input, 0=output)
- Auto-increment within register pairs (not across pairs).
- Burst read/write support for paired registers.
- Open-drain active-low interrupt output (INT) for input state changes.
- Internal ~100 kΩ pullup on each I/O pin.
- No software reset — reset only via power cycle (POR).
- Interrupt errata workaround: after reading input ports, write a command byte ≠ 0x00.
- No polarity inversion changes without explicit API call.
- Push-pull outputs with 25 mA sink / 10 mA source per pin.

---

## Driver Architecture: Passive Cooperative Driver

The driver follows a **passive cooperative** model:

- `bind()` and `begin()` validate and store callbacks without touching I2C.
- Single-transfer APIs may complete synchronously. Synchronous compound
  convenience APIs have fixed transfer counts and documented worst-case latency.
- Complete-image apply/verify and input-read/errata work uses one fixed-capacity
  operation slot. Admission performs zero I/O. The caller supplies a nonzero
  request ID, current time, whole-operation timeout, and transaction budget.
  Safe-direction convenience APIs remain synchronous and use at most two
  callbacks; owner-budgeted configuration uses the complete-image operation.
- A terminal operation result is retained until consumed exactly once. A new
  operation is rejected while work or an unconsumed result exists.
- Cancellation is cooperative between synchronous callbacks. If an input read
  completed, or its command phase may have reached the chip before failure, the
  required nonzero pointer-park cleanup remains observable and bounded before
  cancellation/timeout becomes terminal.
- PCA9555 has no conversion wait, NVM, calibration storage, endurance-limited
  write, or other rare maintenance procedure. Do not add a speculative rare-
  operation framework.
- Health counters are observational only. They never suppress a requested I2C
  operation or take recovery authority from the caller.
- The caller owns serialization, scheduling, transfer timeout selection, retry
  eligibility/policy, absolute admission policy, device health policy, and bus
  recovery.

### DriverState (3 states only)

```cpp
enum class DriverState : uint8_t {
  UNINIT,    // callbacks not bound, or end() called
  READY,     // callbacks bound; last tracked transfer succeeded or none attempted
  DEGRADED   // callbacks bound; last tracked transfer failed
};
```

State transitions:
- `bind()`/`begin()` success -> READY (presence is not implied)
- Any I2C failure in READY -> DEGRADED
- Success in DEGRADED -> READY
- `end()` -> UNINIT

### Transport Wrapper Architecture

All I2C goes through layered wrappers:

```
Public API (readInputs, writeOutputs, etc.)
    ↓
Register helpers (readRegs, writeRegs)
    ↓
TRACKED wrappers (_i2cWriteReadTracked, _i2cWriteTracked)
    ↓  <- _updateHealth() called here ONLY
RAW wrappers (_i2cWriteReadRaw, _i2cWriteRaw)
    ↓
Transport callbacks (Config::i2cWrite, i2cWriteRead)
```

**Rules:**
- Public API methods NEVER call `_updateHealth()` directly
- `readRegs()`/`writeRegs()` use TRACKED wrappers -> health updated automatically
- `probe()` uses RAW wrappers -> no health tracking (diagnostic only)
- There is no library bus-recovery API. Explicit register-image verification
  and reconciliation own only PCA9555 protocol phases.

### Health Tracking Rules

- `_updateHealth()` called ONLY inside tracked transport wrappers.
- State transitions guarded by the bound state (no DEGRADED before bind/begin succeeds).
- NOT called for config/param validation errors (INVALID_CONFIG, INVALID_PARAM).
- NOT called for precondition errors (NOT_INITIALIZED).
- `probe()` uses raw I2C and does NOT update health (diagnostic only).

### Health Tracking Fields

- `_lastOkMs` - timestamp of last successful I2C operation
- `_lastErrorMs` - timestamp of last failed I2C operation
- `_lastError` - most recent error Status
- `_consecutiveFailures` - failures since last success (resets on success)
- `_totalFailures` / `_totalSuccess` - lifetime counters (saturate at max)

---

## Versioning and Releases

Single source of truth: `library.json`. `Version.h` is auto-generated and must never be edited.

SemVer:
- MAJOR: breaking API/Config/enum changes.
- MINOR: new backward-compatible features or error codes (append only).
- PATCH: bug fixes, refactors, docs.

Release steps:
1. Update `library.json`.
2. Update `CHANGELOG.md` (Added/Changed/Fixed/Removed).
3. Update `README.md` if API or examples changed.
4. Commit and tag: `Release vX.Y.Z`.

---

## Naming Conventions

- Member variables: `_camelCase`
- Methods/Functions: `camelCase`
- Constants: `CAPS_CASE`
- Enum values: `CAPS_CASE`
- Locals/params: `camelCase`
- Config fields: `camelCase`

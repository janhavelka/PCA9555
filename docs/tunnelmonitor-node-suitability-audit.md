# PCA9555 suitability audit for TunnelMonitor-node

- Date: 2026-07-19
- PCA9555 revision: `46b441e44e6d43ebfaccb255a3b582122463fe94` (`v2.0.0`)
- TunnelMonitor-node revision: `b708f511964db6c51e949e99c67820476f00f9c7` on branch `docs/mb85rc-suitability-contract-facts`

Scope: read-only audit of both repositories; this report is the only intended source change.

## Conclusion

PCA9555 v2.0.0 is not ready to be added to TunnelMonitor-node as a production dependency without refactoring.

The low-level register implementation is generally strong. It is framework-neutral, uses injected transport callbacks, performs paired register transfers, preloads output latches before enabling outputs, implements the interrupt errata workaround, avoids steady-state heap use, and has broad native tests.

The main problems are above the basic register protocol:

- TunnelMonitor-node does not yet define a PCA9555 device, address, channel map, interrupt pin, safe levels, or health role.
- The current TunnelMonitor output controller requires PWM on two MOSFET channels. PCA9555 is static digital I/O and cannot replace those PWM channels.
- TunnelMonitor has one authoritative I2C owner with deadlines, queues, and bus recovery. The PCA9555 library currently adds its own lifecycle I/O, health/offline gate, recovery flow, and partial job scheduler.
- Several current cache and lifecycle behaviors can leave output state unsafe or make recovery restore the wrong state.
- Target-board continuous HIL, brownout, reset, interrupt, and shared-bus evidence is not complete.

The reasonable target is a small, passive PCA9555 chip driver behind a TunnelMonitor-owned adapter. It should own the chip protocol and safe register ordering, but not the I2C bus, task, request queue, retry eligibility/policy, or offline policy.

That target conflicts with the PCA9555 repository's current binding managed-synchronous, four-state health, presence-checking `begin()`, and manual `recover()` rules (`AGENTS.md:133-158`). Before implementation, make an explicit architecture decision: revise those rules and release a new major passive/owner-driven API, or retain the synchronous/local-health design and formally approve a TunnelMonitor ownership exception. This report recommends revising the PCA9555 contract. Do not hide the conflict in an adapter.

PCA9555 is a possible fit for static relays, enables, digital inputs, and additional on/off outputs. It is not a fit for software PWM over the shared I2C bus.

## Audit boundary

This audit used the current code and architecture documents in both repositories. It did not assume a future schematic or invent an expander pin map.

TunnelMonitor's architecture documents are authoritative for the application. In particular:

- only `I2cTask` may access the bus (`../../TunnelMonitor-node/docs/guidelines/i2c_peripherals.md:30-41`);
- library initialization and recovery must be passive, and a library must not independently latch an offline policy (`../../TunnelMonitor-node/docs/guidelines/dependency_policy.md:23-27`);
- the I2C owner normally advances bounded device work one transfer per poll (`../../TunnelMonitor-node/docs/guidelines/i2c_peripherals.md:100-134`);
- production dependencies must be exact-pinned and stay behind narrow project adapters (`../../TunnelMonitor-node/docs/guidelines/dependency_policy.md:8-12,64-83`).

No TunnelMonitor-node source or documentation was changed.

## Positive findings

These parts of the PCA9555 library should be retained:

1. **Framework-neutral core.** Public headers and `src/` use no Arduino, Wire, ESP-IDF, or FreeRTOS APIs. Transport is injected through fixed function pointers (`include/PCA9555/Config.h:11-71`).

2. **Bus ownership is injectable.** The library does not configure pins, frequency, or the bus. This allows an owner-private TunnelMonitor adapter to call `I2cBackend::transfer()`.

3. **Correct paired-register model.** The implementation limits bulk transfers to the PCA9555 register-pair behavior and supports combined 16-bit values (`include/PCA9555/CommandTable.h:18-44`; `include/PCA9555/PCA9555.h:22-42`).

4. **Safe input-to-output ordering exists.** `configureOutputs()` preloads output latches before clearing direction bits (`src/PCA9555.cpp:1287-1314`). This is the right chip-level order.

5. **Interrupt errata is implemented.** Synchronous input reads can hold the optional lock across the input read and nonzero pointer-park write. If the read succeeds and pointer parking fails, the API preserves the valid input value while returning the terminal park error (`src/PCA9555.cpp:1511-1551,1670-1699`).

6. **Partial-write uncertainty is recognized.** Failed writes can mark hardware/cache state dirty (`src/PCA9555.cpp:1448-1465,1834-1847`). The idea is correct even though the current dirty-state fence is incomplete.

7. **No steady-state heap or logging in the core.** The driver uses fixed local buffers and static error strings.

8. **Good current automated coverage.** The audit run passed 158 of 158 native tests, both Arduino ESP32-S2 and ESP32-S3 builds, the version check, timing guard, CLI contract, ESP-IDF static contract, and host-side HIL contract/parser tests. No physical HIL run was performed for this audit.

These strengths make refactoring preferable to replacing the driver with application-owned register code.

## Hard findings: hardware and product admission

### TM-1 - The target device and channel map do not exist yet

Severity: **integration blocker**

TunnelMonitor-node currently contains no PCA9555 or generic I/O-expander definition. Its board source lists only OLED `0x3C`, RTC `0x51`, FRAM `0x50`, ENV `0x76`, and INA228 `0x41` (`../../TunnelMonitor-node/include/TunnelMonitor/BoardPins.h:80-87`). The current outputs are ESP32 GPIO3, GPIO45, and GPIO46 (`../../TunnelMonitor-node/include/TunnelMonitor/BoardPins.h:89-97`).

The following facts are missing and must come from the board/product design, not from defaults in this library:

- PCA9555 address strap and selected address `0x20` through `0x27`;
- product profiles that contain the device;
- P00-P07/P10-P17 signal assignment;
- input/output direction mask;
- active-high or active-low behavior for each output;
- safe output latch value for every output pin;
- polarity-inversion mask;
- unused-pin policy;
- INT wiring, pull-up, MCU GPIO, and service policy, if INT is used;
- required or optional health role;
- behavior when the device is absent or fails after boot.

The current PCA9555 address range does not collide with the addresses declared by TunnelMonitor. That is only an address compatibility fact; it is not an integration specification or identity proof.

### TM-2 - PCA9555 cannot replace the current PWM outputs

Severity: **hardware/function blocker**

TunnelMonitor's current `mosfet1` and `mosfet2` channels support off, on, and PWM at 100 Hz with 10-bit resolution. The relay is on/off only (`../../TunnelMonitor-node/docs/guidelines/target_architecture.md:355-362`; `../../TunnelMonitor-node/include/TunnelMonitor/contracts/Outputs.h:10-16,220-226`).

PCA9555 provides static push-pull digital outputs. It has no PWM hardware.

Do not add software PWM to this library. Periodic I2C writes would consume shared-bus capacity, add jitter, couple output behavior to bus recovery, and conflict with TunnelMonitor's bounded I2C owner. Keep PWM on ESP32 LEDC or use a dedicated hardware PWM device. Use PCA9555 only for static channels unless the hardware design changes.

### TM-3 - Software cannot guarantee safe OFF during reset or power sequencing

Severity: **hardware safety blocker**

PCA9555 powers up with all pins configured as inputs, output latches high, and an approximately 100 kOhm internal pull-up on each I/O input (`docs/chip_notes.md:30-45,52-63`). TunnelMonitor's current three output-control signals are active high (`../../TunnelMonitor-node/include/TunnelMonitor/BoardPins.h:91-96`).

An active-high MOSFET gate, relay input, or enable input connected directly to a PCA9555 pin can therefore be biased toward ON before firmware configures the expander. A library cannot fix that pre-I2C interval.

The board must provide an electrical safe state, normally with suitable external pull-downs, driver stages, or enable gating. The design must cover:

- cold power-up;
- ESP-only reset while PCA9555 stays powered;
- PCA9555-only brownout or power interruption followed by POR while the ESP remains active;
- watchdog and panic reset;
- firmware update and bootloader time;
- disconnected or recovering I2C bus.

The current TunnelMonitor boot path applies direct-GPIO safe states and starts `OutputController` before it starts I2C (`../../TunnelMonitor-node/src/app/App.cpp:225-252`). The current planned-restart coordinator quiesces measurement/cloud and prepares storage, but it does not command outputs off (`../../TunnelMonitor-node/src/system/RestartCoordinator.cpp:435-488`). An expander-backed product needs an explicit boot and planned-shutdown design. Unplanned reset safety still belongs in hardware.

### TM-4 - Critical mux changes are not atomic across both ports

Severity: **hardware mapping and sequencing blocker**

A paired two-byte write is one I2C transaction, but PCA9555 still exposes separate Port 0 and Port 1 registers and does not document a simultaneous commit of both ports (`include/PCA9555/CommandTable.h:14-27`; `src/PCA9555.cpp:627-635`). The library must not promise that selector or enable bits split across the two ports change at the same instant.

Keep critical mux selector and enable signals in one port where the hardware map allows it. Otherwise define an explicit break-before-make sequence: disable or externally gate the mux, update the selection, then enable it. If a transient cross-port combination is hazardous, hardware gating is required; a library helper cannot create cross-port atomicity.

### TM-5 - The current OutputController boundary cannot call an I2C expander

Severity: **application architecture blocker**

`OutputController` is a cooperative main-loop owner. Its `OutputBackend` methods are immediate synchronous GPIO/LEDC configure/write calls with no request identity or deadline (`../../TunnelMonitor-node/include/TunnelMonitor/output/OutputController.h:14-41`). The default backend calls Arduino GPIO and LEDC directly (`../../TunnelMonitor-node/src/output/OutputController.cpp:129-193`).

Only `I2cTask` may call the I2C backend. Therefore an `OutputBackend` implementation must not directly call PCA9555 or wait for an I2C result.

The later TunnelMonitor integration needs this ownership path:

```text
Output policy / OutputController
            |
            | bounded command and exact completion identity
            v
         I2cTask  ---- owns deadlines, queue, retry eligibility/policy, recovery and bus
            |
            v
 owner-private PCA9555 adapter/library ---- owns PCA9555 register protocol
            |
            v
        I2cBackend
```

This requires a bounded pending state in the output owner and append-only I2C command/result changes. It is TunnelMonitor work for a later dedicated prompt; it is not a reason to put a queue, task, or application registry in the PCA9555 library.

### TM-6 - TunnelMonitor has no contract slots for the device

Severity: **application contract blocker**

The current I2C operation enum ends at `Scan = 9` (`../../TunnelMonitor-node/include/TunnelMonitor/contracts/FieldBus.h:13-25`). `DeviceId` has no expander (`../../TunnelMonitor-node/include/TunnelMonitor/contracts/Health.h:76-97`). The known-device count is five, and the table contains RTC, FRAM, ENV, power, and display only (`../../TunnelMonitor-node/include/TunnelMonitor/i2c/I2cConfig.h:82-83`; `../../TunnelMonitor-node/src/i2c/I2cDiagnostics.cpp:96-105`).

Later integration needs append-only device and operation values, fixed command/result payloads, deadlines, health/status projection, events, native fake coverage, and HIL. The planned optional-I2C-leaf platformization step covers ENV, power, and display, not output transport (`../../TunnelMonitor-node/prompts/prompt_45g_optional_i2c_leaf_descriptors.md:3-34`). Do not hide PCA9555 integration inside that unrelated scope.

## Hard findings: PCA9555 library correctness and safety

### LIB-1 - Dirty state does not fence cache-based writes

Severity: **must fix before output use**

A failed write marks `_hardwareStateDirty` (`src/PCA9555.cpp:1458-1465,1834-1841`). However, `_normalOperationStatus()` checks only the OFFLINE state (`src/PCA9555.cpp:1603-1607`).

Cache-based methods such as `writePin()`, set/clear/toggle helpers, preload helpers, and direction helpers continue to build full-byte or full-pair writes from cached values (`src/PCA9555.cpp:691-719,778-975,982-1038,1242-1314`). `_hardwareStateCleanStatus()` is mainly checked on no-op branches.

After an uncertain or partial write, the cached value may be wrong. A later single-pin update can then overwrite unrelated bits in the same port.

Required refactor:

- block every cached read-modify-write operation while relevant state is invalid;
- allow only explicit full-state reconciliation while dirty; or
- track validity per register pair if there is a proven need for partial availability.

A global dirty fence is simpler and safer for this device.

### LIB-2 - Readback while local state is considered clean overwrites recovery state

Severity: **must fix before unattended use**

Successful output, configuration, and polarity reads update the corresponding cache pair and active `_config` fields while `_hardwareStateDirty == false` (`src/PCA9555.cpp:668-688,741-760,1061-1103,1149-1188,1344-1373,1795-1817`). The dirty guard correctly protects recovery state after a known failed write. It cannot detect an independent PCA9555 power interruption, so local state can still be considered clean after the chip returns to POR. `recover()` later reapplies `_config` (`src/PCA9555.cpp:450-481,1630-1667`).

This mixes two different things:

- the state the application wants the device to have;
- the state most recently observed in hardware.

Example failure:

1. The application has safe output latches low and selected pins configured as outputs.
2. PCA9555 loses power independently and returns to POR while the ESP remains active.
3. One or more diagnostic reads succeed while the library still considers state clean.
4. Each read replaces its corresponding output, configuration, or polarity recovery pair with the observed POR value.
5. After the respective writable pairs have been read, `recover()` can reapply POR values instead of the application's expected image.

Required refactor:

- accept the complete expected register image from the caller, which remains the application source of truth;
- keep only a validity-qualified protocol shadow when needed for chip-level read-modify-write operations;
- keep a distinct observed snapshot with validity flags and timestamp/evidence;
- never change the caller's expected state or the write-intent shadow from a read-only operation;
- add an explicit compare/verify operation;
- require any adoption of observed state to be an explicit caller policy decision, outside the library.

### LIB-3 - Failed or repeated begin can leave active outputs unmanaged

Severity: **must fix before output use**

`begin()` clears the current instance state before it validates the new configuration (`src/PCA9555.cpp:80-110,146-159`). Calling `begin()` on an already initialized instance with invalid configuration therefore loses driver control while the existing hardware outputs remain active.

There is a second failure case. `begin()` can successfully write output, polarity, and direction registers, then fail during the final input read or errata pointer park (`src/PCA9555.cpp:181-190,1630-1667`). The cleanup returns the object to UNINIT and discards the applied configuration. If the final failure was a read failure, no write failure marks the device dirty. Hardware may still be actively driving outputs, but the driver reports an uninitialized, clean local state.

Required refactor:

- validate all configuration before changing a live instance;
- reject repeated initialization unless an explicit reconfigure operation is used;
- do not discard the last controlled state after a late initialization failure;
- report the failed phase and retain enough state to reconcile safely;
- do not use an automatic all-input rollback as a universal safe policy, because input pull-ups may also be unsafe for active-high loads.

### LIB-4 - `end()` applies an application policy and cannot report failure

Severity: **must fix before output use**

`end()` performs a raw write that changes both direction registers to inputs, ignores the error at the call boundary, clears configuration and health, and returns `void` (`include/PCA9555/PCA9555.h:108-112`; `src/PCA9555.cpp:197-243`). A later diagnostic can see dirty state, but the caller cannot make a direct shutdown decision from the return value.

All-input is not a universal safe state. PCA9555 inputs retain internal pull-ups, so an active-high control can rise when changed to input.

Required refactor:

- make `end()` a local detach/reset with no hardware I/O;
- provide a separate explicit, fallible operation to apply an application-supplied safe register image;
- let the I2C owner decide the shutdown deadline and what to do on failure.

### LIB-5 - The default startup rule rejects normal warm restarts

Severity: **must fix for field restart behavior**

`requireConfigPortDefaults` defaults to `true` (`include/PCA9555/Config.h:104-114`). `begin()` rejects any configuration pair that is not `0xFFFF` (`src/PCA9555.cpp:164-179`).

That is useful for a manufacturing POR check, but it is a poor default for an MCU-only restart when PCA9555 remains powered and correctly retains active configuration.

Required refactor:

- add a separate zero-I/O `bind()` for configuration validation and transport binding; do not silently redefine `begin()` while the current repository contract requires a presence read;
- make POR-default checking an explicit diagnostic/startup policy;
- make the production path read, compare, and safely apply a caller-supplied expected image without requiring proof of a PCA9555 power cycle;
- continue to document that address response is not chip identity, because PCA9555 has no chip-ID register.

### LIB-6 - Public raw configuration writes bypass safe preload ordering

Severity: **must fix or restrict before production use**

The public `writeRegister()` and `writeRegisters()` methods accept Configuration registers and write them directly (`include/PCA9555/PCA9555.h:613-630`; `src/PCA9555.cpp:1378-1400`). They do not preload output latches before clearing direction bits. This bypasses the safety rule enforced by typed direction methods.

Required refactor:

- remove writable raw-register access from the stable production API; or
- isolate it in an explicitly unsafe diagnostic surface that production adapters do not expose.

Read-only diagnostic register snapshots are reasonable if they never change caller intent or the write-intent shadow.

### LIB-7 - There is no state-integrity check after an independent PCA power interruption

Severity: **must fix for unattended recovery**

`probe()` reads one configuration byte and proves only that an address responds (`src/PCA9555.cpp:429-447`). Any successful transfer can move local health to READY (`src/PCA9555.cpp:1563-1600`). A PCA9555 that lost power and returned to POR can therefore ACK normally while its outputs, direction, and polarity no longer match the application's intended state.

Required refactor:

- add a `verifyState(callerExpectedImage)`-style operation that reads all writable register pairs;
- compare observed values to the caller-supplied expected image without changing any write-intent shadow;
- return a distinct state-mismatch result;
- let the owner decide when to reconcile.

### LIB-8 - Invalid `offlineThreshold` is silently corrected

Severity: **contract defect**

`Config::offlineThreshold` documents a valid range of 1 through 255 (`include/PCA9555/Config.h:117`), but `begin()` silently converts zero to one (`src/PCA9555.cpp:159-162`). Silent correction hides configuration mistakes.

Return `INVALID_CONFIG` for zero. If local offline gating is removed by the architecture decision, remove this field and its validation together.

## Hard findings: owner, operation, and error contracts

### ARCH-1 - Lifecycle and recovery do too much synchronous work

Severity: **architecture blocker**

`begin()` performs a configuration read and then calls `_applyConfig()` (`src/PCA9555.cpp:164-190`). `_applyConfig()` can perform output, polarity, configuration, input-read, and errata-pointer-park transfers (`src/PCA9555.cpp:1630-1675`). `recover()` performs another read followed by the same apply sequence (`src/PCA9555.cpp:450-483`).

The current timeout is applied separately to every callback, not as one whole-operation deadline. With a 50 ms transfer timeout, a lifecycle call can consume several timeout windows. TunnelMonitor's normal I2C poll budget is 5 ms, normal transfer timeout is 20 ms, and command deadline is normally 1000 ms (`../../TunnelMonitor-node/include/TunnelMonitor/i2c/I2cConfig.h:60-76`).

Required refactor:

- add a passive zero-I/O `bind()` operation;
- express initialization, state verification, safe configuration, and reconciliation as bounded operations with explicit phases;
- execute at most one physical transfer per normal owner poll;
- let `I2cTask` and its adapter carry the absolute operation deadline and supply a per-transfer timeout no greater than the remaining time;
- let the owner grant exclusive continuation between phases that must not be interleaved;
- expose reset/cancel-safe phase state so the owner can drop an expired operation;
- do not add an internal queue, task, sleep, retry loop, or bus recovery.

The current public chunk-job API is not a sufficient solution. It covers only selected operations, has no absolute deadline or cancellation contract, and introduces a second public scheduling model for an eight-register device (`include/PCA9555/PCA9555.h:102-166`; `src/PCA9555.cpp:250-401`). Replace it with narrow operation phase state that the sole bus owner advances.

This refactor changes the current managed-synchronous, presence-checking `begin()`, four-state health, and manual recovery rules in `AGENTS.md:133-158`. It cannot be implemented as written until that binding contract is revised, or TunnelMonitor formally accepts an ownership exception. Prefer a new zero-I/O `bind()` rather than redefining `begin()` under the current rule.

### ARCH-2 - Recovery and OFFLINE policy have two owners

Severity: **architecture blocker**

PCA9555 tracks consecutive failures, latches `DriverState::OFFLINE`, blocks normal I/O, and requires its own `recover()` (`src/PCA9555.cpp:1563-1624`). TunnelMonitor already owns timeout classification, automatic retry, bus reset, SCL recovery, backoff, fault thresholds, and result delivery.

If TunnelMonitor recovers the bus but the library remains locally OFFLINE, ordinary PCA operations remain blocked until a second PCA-specific recovery path runs. This creates two recovery authorities and two health state machines.

Required refactor:

- keep transport counters and last-error evidence observational if useful;
- do not reject transfers because a local counter crossed a threshold;
- keep bus recovery and retry entirely in `I2cTask`;
- rename chip-state reapplication to reconciliation rather than bus recovery;
- map one terminal library operation into TunnelMonitor's device status and health.

### ARCH-3 - Interrupt errata handling must be mandatory and non-interleaved

Severity: **must fix if inputs or INT are used**

The synchronous input path can correctly protect the input-read plus pointer-park sequence. The public job path splits those steps across polls and explicitly does not retain the optional lock (`include/PCA9555/PCA9555.h:118-122`; `src/PCA9555.cpp:1704-1728`). The configuration also allows the workaround to be disabled (`include/PCA9555/Config.h:113-114`).

Required refactor:

- make the workaround the production behavior for every input-register read;
- keep the bus owner exclusive from input read through pointer park;
- if the two transfers are stepped across owner polls, the active operation must prevent any other target transfer from interleaving;
- keep an opt-out only for explicit bench/test use, if it is retained at all.

### ARCH-4 - Transport result and operation state are mixed

Severity: **must fix before adapter integration**

Transport callbacks are documented as synchronous (`include/PCA9555/Config.h:11-42`), but `Err::IN_PROGRESS` is also accepted as a transport outcome (`include/PCA9555/Status.h:19-27`). In a job, an IN_PROGRESS result leaves the same phase active, so the next poll can issue the same transfer again (`src/PCA9555.cpp:338-383,1702-1777`). A write can therefore be replayed without a defined completion channel or retry deadline.

The error enum also contains both `TIMEOUT` and `I2C_TIMEOUT`. It declares `OFFLINE`, but offline paths return `BUSY` (`include/PCA9555/Status.h:10-27`; `src/PCA9555.cpp:345-348,1603-1606`). TunnelMonitor exposes a generic NACK, not a truthful address-NACK versus data-NACK distinction (`../../TunnelMonitor-node/include/TunnelMonitor/contracts/FieldBus.h:27-38`).

Required refactor:

- transport callbacks must return terminal transfer results only;
- use a separate operation state such as Pending/Complete;
- distinguish not-attempted from attempted-and-failed;
- represent whether a write may have committed;
- define exact timeout semantics: owner operation-deadline expiry belongs to `I2cTask`/the adapter, while a backend transfer timeout is a distinct terminal transport result;
- remove duplicate timeout codes only when they describe the same event;
- do not require an adapter to invent address-NACK versus data-NACK evidence;
- use OFFLINE consistently if it remains public, or remove it with the local offline gate.

A small typed result is clearer than allowing callbacks to return any driver-level `Status`.

### ARCH-5 - Expected-state writes need ambiguous-write reconciliation

Severity: **must fix for safe output commands**

An I2C timeout or bus failure during a write may occur after some or all bytes reached the device. The current dirty flag is conservative, but subsequent behavior is not sufficiently constrained and there is no operation-level readback reconciliation.

Required refactor:

- classify retry eligibility by operation phase and payload semantics;
- mark the affected observed pair invalid;
- read back the affected register pair;
- report success only if the intended state is verified;
- otherwise retain dirty/mismatch evidence and require explicit reconciliation.

PCA9555 output, polarity, and configuration writes carry absolute register bytes, so replaying the identical full-register payload is idempotent at the register level. The owner may use a bounded retry when that exact phase is explicitly classified retry-safe. It must not advance from an uncertain latch-preload phase into direction-enable until the latch has been successfully reapplied or verified, and it must never report an indeterminate terminal write as successful.

## Recommended library shape

The following names are illustrative. The important part is the ownership and state contract.

### Small stable types

```cpp
enum class Pin : uint8_t {
  P00, P01, P02, P03, P04, P05, P06, P07,
  P10, P11, P12, P13, P14, P15, P16, P17
};

enum class Direction : uint8_t { Input, Output };
enum class Level : uint8_t { Low, High };
using PinMask = uint16_t;

struct RegisterImage {
  uint16_t outputs;
  uint16_t polarity;
  uint16_t directions;  // one bit means input, matching the chip register
};

struct ObservedState {
  RegisterImage registers;
  uint8_t validPairs;
};
```

Useful constexpr helpers:

- `pinMask(Pin)`;
- `portOf(Pin)` and `bitOf(Pin)`;
- `isValidAddress(uint8_t)`;
- `isOutput(RegisterImage, Pin)`;
- `levelFor(RegisterImage, Pin)`;
- `PortData`/`uint16_t` conversion as `constexpr`.

Board signal names such as fan, router, relay, valve, or mux channel must remain outside the library.

### Keep application intent separate from observed state

TunnelMonitor's output policy owner must remain the source of truth for the expected `RegisterImage`. Apply, verify, and reconcile operations should take that complete caller-supplied image. The driver may retain a validity-qualified protocol shadow for efficient chip-level operations, but that shadow is not application policy. Reads populate `ObservedState`; they do not change caller intent or write intent.

After a write, the driver records one of:

- applied and observed/assumed valid;
- not attempted;
- failed with no commit possible;
- outcome indeterminate, readback required;
- verified mismatch.

This model removes the current unsafe cache ambiguity.

### Passive lifecycle

Under the recommended owner-driven contract, add `bind()` to validate and store callbacks and chip configuration with zero device I/O. It must preserve a live instance on validation failure. Keep `begin()` behavior unchanged until the binding repository contract is explicitly revised; do not give one method incompatible meanings within the same major version.

Device work should be explicit:

- probe address response;
- read complete writable state;
- `verifyState(callerExpectedImage)`;
- `applyRegisterImage(callerSuppliedImage)` with latch-before-direction ordering;
- read inputs and perform pointer park;
- write a complete output pair;
- write masked outputs using a valid protocol shadow;
- configure selected outputs with latch preload first;
- configure selected inputs;
- reconcile against a caller-supplied image after mismatch, power interruption/POR, or indeterminate write.

Multi-transfer work should expose narrow phase state for the owner to advance and reset safely. `I2cTask`/the adapter owns the absolute deadline, per-transfer remaining-time calculation, exclusive continuation, cancellation, and retry decision. The library should not contain its own queue, time source, wait, retry loop, or recovery policy.

### Small public API

The current public header is 783 lines and exposes roughly 55 fallible methods for eight registers. A smaller production surface will be easier to verify.

Keep one canonical method for each operation. Single-pin set/clear/toggle helpers may be thin wrappers around one masked-output method. Deprecate or remove:

- bool-based direction alongside typed direction;
- duplicate set/clear/toggle variants that add no contract value;
- the broad public chunk scheduler;
- public errata primitives that allow callers to split the required sequence;
- public writable raw-register access;
- diagnostics that silently mutate write-intent state.

### Diagnostics and enums

Reasonable helpers include:

- stable `errorName()` and `operationName()` functions returning static strings;
- a compact health/evidence snapshot without transport callback pointers;
- a complete read-only register snapshot;
- last failed phase and whether a write outcome was indeterminate;
- saturating counter documentation that matches the implementation.

Current docs say lifetime counters wrap at max, while the implementation saturates them (`include/PCA9555/PCA9555.h:249-255`; `src/PCA9555.cpp:1571-1592`). Keep saturation and fix the text.

## TunnelMonitor work required later

Do not implement these items as part of the PCA9555 library refactor:

1. Add the expander address and pin/signal map to a compile-time board/product descriptor.
2. Decide which static signals move to PCA9555 and which PWM signals remain on ESP32 LEDC.
3. Keep critical mux signals within one port or specify break-before-make and required external gating.
4. Add append-only `DeviceId` and I2C operation contracts with fixed payloads and exact result identity.
5. Add an owner-private PCA9555 adapter called only by the I2C worker.
6. Add a bounded pending state to the output owner instead of blocking in `OutputBackend`.
7. Define safe boot, all-off, planned restart, and fault behavior.
8. Project device presence, mismatch, dirty state, and terminal command results into existing health/status surfaces.
9. Exact-pin the reviewed PCA9555 commit in firmware and native builds.
10. Add native fake-owner tests and target-board HIL.

Use compile-time static descriptors. Do not add a runtime registry, plugin system, dynamic container, or generic hardware manager.

## Explicit non-requirements

The PCA9555 library does not need:

- software PWM;
- a FreeRTOS or Arduino task;
- an I2C bus manager;
- bus initialization or pin configuration;
- hidden retries;
- SCL recovery or device power cycling;
- application queues or result rings;
- logging;
- board-specific channel names;
- debounce or fan/router automation policy;
- a runtime plugin/driver registry;
- a Wire compatibility facade in the core.

## Verification and release findings

### Current automated result

Audit commands passed on 2026-07-19:

- `python -m platformio test -e native`: 158/158 tests passed;
- `python -m platformio run -e esp32s2dev`: passed;
- `python -m platformio run -e esp32s3dev`: passed;
- generated-version check: passed;
- core timing/framework guard: passed;
- Arduino CLI contract: passed;
- ESP-IDF static example contract: passed;
- host-side HIL contract and parser tests: passed;
- `python -m platformio pkg pack`: completed.

The local PlatformIO executable reported Core 6.1.18 while repository comments and CI specify 6.1.19. This did not cause a failure, but release evidence should use the pinned tool version.

A local native ESP-IDF build was not run because `idf.py` was unavailable. CI is configured to build the native ESP-IDF example for ESP32-S2 and ESP32-S3 with ESP-IDF 5.4 (`.github/workflows/ci.yml:121-139`). A current CI result was not independently retrieved during this audit.

### HIL is not a production pass

The retained HIL summary calls the library a pre-production candidate. It states that continuous HIL remained blocked by the USB CDC command channel and explicitly says not to claim production-grade or fully hardware-validated status (`docs/reports/hil-validation-summary-20260625.md:3-8,16-38`).

Before TunnelMonitor field adoption, run target-board tests for:

- 400 kHz shared-bus traffic with all enabled TunnelMonitor devices;
- PCA input read and errata pointer park with proof that no transfer interleaves;
- cold boot and ESP-only reset;
- PCA9555-only brownout or power interruption followed by POR while the ESP remains active;
- safe inactive output levels before, during, and after firmware startup;
- write timeout with possible commit and readback reconciliation;
- unplug/replug, bus-stuck recovery, and missing device behavior;
- planned restart and watchdog reset behavior;
- each used input and static output pin;
- INT assertion/clear behavior if INT is wired;
- a continuous soak with reset count, uptime, queue, timeout, and recovery telemetry.

### Package and consumer validation need cleanup

The PlatformIO package currently has no curated export filter (`library.json:42-48`). The packed archive includes repository tooling and tracked build logs, while excluding `docs/` even though the packaged README links into those docs. CI checks that packing succeeds, but it does not unpack the artifact and compile a clean consumer (`.github/workflows/ci.yml:91-119`).

Before release:

- define explicit package contents;
- remove tracked `build_output.txt` and `build_result.txt` from the package/repository as appropriate;
- either ship linked docs or use repository web links;
- unpack the produced archive and build a minimal callback-based consumer;
- add a minimal owner-adapter compile/test fixture that does not depend on `examples/common`.

Related documentation cleanup:

- `Doxyfile` still declares project version 1.1.0 while manifests are 2.0.0;
- `SECURITY.md` lists 1.0.x as supported and contains unrelated NVS wording;
- `CONTRIBUTING.md` refers to a missing `.clang-format`;
- the README installation example uses unpinned Git HEAD, while TunnelMonitor requires an exact immutable revision.

These are not chip-protocol defects, but they should be fixed before presenting the package as a production platform dependency.

## Required test additions after refactor

PCA9555 library/native tests should cover at least:

- invalid repeated begin preserves the live instance;
- late initialization failure retains a controllable fault state;
- every cached RMW method rejects invalid/dirty shadow state;
- a successful read after simulated PCA POR does not replace caller intent or write intent;
- state verification reports exact pair mismatch;
- production startup accepts retained non-default state and reconciles it;
- POR-default checking remains available as an explicit diagnostic;
- no retry loop or retry policy occurs inside the library;
- not-attempted and possibly-committed failures map differently;
- ambiguous output/configuration writes use readback reconciliation;
- retry-safe absolute-register writes do not advance an unsafe later phase prematurely;
- phase progress and reset/cancel-safe state are deterministic;
- input read and pointer park preserve their operation state across phases;
- direct public APIs cannot bypass latch-before-direction ordering;
- error names and OFFLINE/BUSY behavior match the public contract.

TunnelMonitor owner/adapter tests should separately cover:

- one absolute owner deadline across every library phase;
- per-transfer timeout never exceeding the remaining operation time;
- queue/result correlation and exact completion identity;
- timeout cancellation/drop behavior;
- non-interleaving and exclusive ownership from input read through pointer park;
- phase-aware bounded retry and readback reconciliation;
- break-before-make sequencing for any mux signals that cannot share one port.

Do not target an arbitrary coverage percentage. Cover the safety state transitions, uncertain-write cases, and owner boundary.

## Recommended sequence

1. Resolve the binding architecture decision: revise the PCA9555 managed-synchronous/local-health rules for a new major API, or formally approve the TunnelMonitor exception.
2. Freeze the hardware/product facts: static channels, address, pin map, same-port or break-before-make mux sequencing, safe electrical bias, INT, and power-loss/POR behavior.
3. Refactor PCA9555 caller-expected/observed state, protocol-shadow validity, dirty fencing, begin/end behavior, and transport result contract.
4. Replace the broad chunk scheduler and multi-timeout lifecycle with passive binding plus narrow owner-driven phases; keep the absolute deadline in `I2cTask`/the adapter.
5. Reduce and strongly type the public API; restrict raw writes.
6. Add the missing native safety tests and a packed-artifact consumer test.
7. Review and exact-pin a new major library revision.
8. In a separate TunnelMonitor scope, add the owner-private adapter, fixed contracts, output pending state, boot/restart policy, and product descriptors.
9. Run target-board HIL and a continuous shared-bus soak before field release.

This sequence keeps chip protocol in the PCA9555 library, application policy in TunnelMonitor, and I2C ownership in `I2cTask`.

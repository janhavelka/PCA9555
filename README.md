# PCA9555 Driver Library

Framework-neutral C++17 driver for the TI PCA9555 16-bit I/O expander. The
library owns the chip register protocol. The application owns the I2C bus,
serialization, scheduling, retry policy, bus recovery, and device health
policy.

Version 3.0.1 is a pre-production candidate. Native tests and compile checks are
necessary, but real-board electrical, interrupt, brownout, and shared-bus
validation are still required before a field-readiness claim.

## Design summary

- `bind()` and its compatibility alias `begin()` validate and store callbacks.
  They perform no I2C.
- `detach()` and `end()` perform no I2C.
- `probe()` is the explicit address-response check. The PCA9555 has no identity
  register, so an ACK is not chip identity proof.
- Transport callbacks return one terminal `TransportResult` for exactly one
  physical attempt. The driver does not retry or recover the bus.
- Plain register APIs are synchronous and use one callback. Input APIs use at
  most two callbacks for read plus pointer park. Safe direction APIs use at
  most two callbacks for latch preload plus direction write.
- Compound apply, verify, and input-read work has one fixed cooperative
  operation slot with a caller-supplied request ID, admission time,
  whole-operation timeout, and transfer budget.
- Writable intent, observed register values, shadow validity, and uncertain
  write effects are separate state.
- Public pin, port, level, and direction arguments use typed enums.
- The core uses no Arduino, ESP-IDF, heap allocation, logging, delay, task, or
  bus-global state.

## Electrical safety

- PCA9555 outputs are push-pull. Do not use them as power drivers. Check the TI
  data sheet's per-pin and package current limits for the actual board voltage
  and load.
- After power-on reset, all pins are inputs, output latches are high, and the
  device has weak internal pull-ups. Hardware must provide a safe state during
  reset and before firmware can communicate.
- Preload output latches before changing pins from input to output. Use
  `configureOutputs()` for a synchronous update or `startApplyImage()` for a
  complete verified image.
- INT is active-low and open-drain and needs an external pull-up. Library I2C
  methods are not ISR-safe; defer service to the bus owner.
- Input sense includes configured polarity inversion. Output-latch readback is
  not proof of physical pin voltage.
- The PCA9555 has no software reset. A caller-owned recovery policy can apply
  and verify a known register image, but cannot reproduce a power cycle.

See [chip notes](docs/chip_notes.md), the
[register reference](docs/register_reference.md), and the
[hardware validation runbook](docs/hardware_validation.md).

## Installation

Production users should pin an audited release tag or immutable commit. Do not
track a moving branch. Version 3.0.1 can be pinned as:

```ini
lib_deps =
  https://github.com/janhavelka/PCA9555.git#v3.0.1
```

For a manual install, copy `include/PCA9555/` and `src/`. The repository is also
an ESP-IDF component through its root `CMakeLists.txt` and
`idf_component.yml`.

## Bind and probe

The application adapter must map its transport result to the library's typed
terminal result. It must not retry, recover the bus, retain buffer pointers, or
call the same driver recursively.

```cpp
#include <PCA9555/PCA9555.h>

PCA9555::TransportResult writeOnce(
    uint8_t address, const uint8_t* data, size_t size,
    uint32_t timeoutMs, void* user);

PCA9555::TransportResult writeReadOnce(
    uint8_t address, const uint8_t* tx, size_t txSize,
    uint8_t* rx, size_t rxSize, uint32_t timeoutMs, void* user);

PCA9555::PCA9555 device;
PCA9555::Config config;
config.i2cWrite = writeOnce;
config.i2cWriteRead = writeReadOnce;
config.i2cUser = &myBusAdapter;
config.i2cAddress = 0x20;
config.i2cTimeoutMs = 20;

PCA9555::Status status = device.bind(config);  // zero I2C
if (!status.ok()) {
  // Configuration is invalid. The previous valid binding, if any, is kept.
}

status = device.probe();  // one explicit, untracked address-response check
if (!status.ok()) {
  // Caller decides whether and when to retry or recover the bus.
}
```

For a successful write, report the complete transmitted byte count; the driver
then classifies the write as committed. On a register-write failure, report
`WriteEffect::NOT_ATTEMPTED` only when the adapter proves that no register data
was accepted. For write-then-read, the same field describes the command write
phase: `NOT_ATTEMPTED` proves the command byte was not accepted, `COMMITTED`
proves it was accepted, and `MAY_HAVE_COMMITTED` is the conservative fallback.
Always report completed byte counts that are known at the device boundary.
Short successful transfers are rejected by the driver.

`Config::i2cTimeoutMs` bounds each callback. It does not configure the bus or
replace the whole-operation deadline.

## Error handling

Every fallible API returns `Status`. The driver never logs, throws, retries, or
turns a failed callback into success. `Status::msg` always points to static
storage; `Status::detail` carries error-specific numeric evidence.

| Status | Meaning and caller action |
| --- | --- |
| `INVALID_CONFIG`, `INVALID_PARAM`, `NOT_INITIALIZED` | Local validation or binding failure. No callback is invoked. |
| `BUSY` | Another operation owns the device, a result is pending, or the request ID does not match. Inspect `BusyDetail`; do not bypass ownership. |
| `DEVICE_NOT_FOUND` | Explicit `probe()` received an address NACK. This is address-response evidence, not chip identity. |
| `I2C_NACK_ADDR`, `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, `I2C_ERROR` | One transport callback failed. The adapter detail is retained and health is updated for tracked calls. |
| `TIMEOUT` | The caller-owned cooperative operation deadline expired or the caller explicitly requested timeout. It is distinct from one callback timing out. |
| `SHADOW_INVALID`, `STATE_UNCERTAIN` | Cached read-modify-write is unsafe. Verify or apply a complete caller-owned image before continuing. |
| `VERIFY_MISMATCH` | Readback did not match the complete expected image. Inspect `OperationResult::mismatchPairs`. |
| `IN_PROGRESS`, `NO_RESULT`, `CANCELLED` | Cooperative lifecycle evidence; keep the exact request ID and consume each terminal result once. |

Read output parameters only when the corresponding receive completed. Input
APIs deliberately retain valid received data even if the following mandatory
pointer park fails; in that case the returned `Status` reports the park failure
while the data output is valid. Cooperative callers use
`OperationResult::observed.valid(PAIR_INPUTS)` and the cleanup fields to make
the same distinction explicitly.

## Typed synchronous APIs

```cpp
using namespace PCA9555;

// First establish and verify a complete caller-owned RegisterImage with the
// cooperative startApplyImage()/pollOperation()/takeOperationResult() flow.
Status status = device.preloadOutput(Pin::P03, Level::LOW_LEVEL);
if (status.ok()) status = device.setDirection(Pin::P03, Direction::OUTPUT_MODE);
if (status.ok()) status = device.writePin(Pin::P03, Level::HIGH_LEVEL);

Level sense = Level::LOW_LEVEL;
if (status.ok()) status = device.readPin(Pin::P10, sense);

PortData outputs{};
if (status.ok()) status = device.readOutputs(outputs);
if (!status.ok()) {
  // Caller handles the exact error; the driver does not retry.
}
```

The `Pin` names match the data sheet: `P00` through `P07`, then `P10` through
`P17`. A `PinMask` uses bit 0 for P00 and bit 15 for P17. Helpers such as
`pinMask()`, `portOf()`, `bitOf()`, `isOutput()`, and `levelFor()` keep mapping
logic out of application code.

Cached read-modify-write helpers require a valid, certain shadow for the
affected register pair. If it is missing or uncertain they fail explicitly;
apply and verify a complete caller-owned `RegisterImage` instead of guessing.

## Cooperative operations

The driver has one fixed operation slot. Starting an operation admits work but
performs zero I2C and returns `Err::IN_PROGRESS`. A nonzero request ID identifies
every poll, cancellation, timeout, and result take.

```cpp
PCA9555::RegisterImage target;
target.outputs = 0xFFFF;     // desired output latches
target.polarity = 0x0000;    // normal input polarity
target.directions = 0xFFFF;  // all pins input

const uint32_t requestId = 17;
const uint32_t admittedAtMs = nowMs();
PCA9555::Status status = device.startApplyImage(
    requestId, target, admittedAtMs, 100);

while (status.inProgress()) {
  uint8_t used = 0;
  status = device.pollOperation(requestId, nowMs(), 1, used);
  // Return to the owner scheduler after this bounded poll.
}

PCA9555::OperationResult result;
status = device.takeOperationResult(requestId, result);
if (status.ok() && result.outcome == PCA9555::OperationOutcome::SUCCEEDED) {
  // The complete image was read back and matched.
}
```

In a real cooperative owner, do not use a blocking loop. Poll once per owner
cycle with the intended transaction budget.

### Fixed transfer bounds

| Operation | Maximum callbacks | Phases |
| --- | ---: | --- |
| `startApplyImage()` | 8 | write outputs, polarity, directions; verify all three; read inputs; pointer park |
| `startVerifyImage()` | 3 | read and compare outputs, polarity, directions |
| `startReadInputs()` | 2 | read both input ports; pointer park |

These bounds are exposed as `MAX_APPLY_IMAGE_TRANSACTIONS`,
`MAX_VERIFY_IMAGE_TRANSACTIONS`, and `MAX_READ_INPUTS_TRANSACTIONS`. The final
pointer-park transfer after a completed input read, or after a failed receive
whose command may have reached the chip, is mandatory and cannot be disabled.

The whole-operation timeout must be from 1 through `INT32_MAX` milliseconds.
Deadline comparison is wrap-safe. Before each cooperative callback, the driver
derives a positive per-transfer allowance no greater than
`Config::i2cTimeoutMs` and shares the remaining deadline budget across the
callbacks allowed in that poll. A callback already in progress remains
synchronous and is bounded by that supplied allowance.

Cancellation and caller-forced timeout are cooperative. If an input read
completed, or its command phase may have reached the chip before the receive
failed, pointer-park cleanup remains required and gets one bounded attempt
before the terminal result becomes available. The original read failure stays
the primary result; cleanup outcome is reported separately in
`OperationResult`. `cleanupAfterDeadline` is set only when that cleanup is
polled at or after the stored operation deadline; an explicit earlier
`timeoutOperation()` request does not set it.

A terminal result remains pending until taken exactly once. While work is
active or a result is pending, new operation admission and binding replacement
return `Err::BUSY` with a stable `BusyDetail`. A wrong request ID cannot consume
or advance another request. While an operation is active, other public I2C
methods return `Err::BUSY` without invoking a callback, so no synchronous call
can interleave between its phases.

`detach()`/`end()` never perform I2C. Detaching during ordinary active work
cancels it and retains the terminal result for one take. If an input transaction
already owes pointer-park cleanup, detach returns `Err::BUSY` and keeps the
binding so the caller can give that cleanup its bounded poll.

## Register state and write ambiguity

`RegisterImage` is caller-owned desired state. The driver's internal shadow is
only protocol state established by complete, certain pair writes or a successful
apply/explicit verify against a complete caller image. `ObservedState` is
readback evidence and never becomes caller intent implicitly.

Relevant evidence is explicit:

- `ObservedState::validPairs`: complete register pairs read as observation;
- `shadowValidPairs`: protocol-shadow pairs safe for cached RMW;
- `mismatchPairs`: observed writable pairs that did not match the expected
  image;
- `uncertainPairs`: pairs whose hardware effect cannot be proved;
- `WriteEffect`: whether one callback's relevant device-side transmit phase was
  not attempted, committed, or may have committed. Successful apply/verify
  outcomes and readback evidence report operation-level verification separately.

When a write may have committed, the affected shadow is invalidated before a
later cached update can overwrite unrelated bits. Use `startVerifyImage()` to
reconcile pairs whose readback matches a complete known expectation, or
`startApplyImage()` to write and verify a complete safe image. The library does
not select that image or retry it.

Raw reads update observed evidence and invalidate a protocol-shadow pair when
readback contradicts it; they never adopt readback as caller intent. Raw
Configuration-register writes are rejected because they bypass latch preload.
Other raw writes invalidate the whole affected shadow pair before the attempt
and only re-establish it after a complete two-register write.

## Health and ownership

`DriverState` has exactly three states:

- `UNINIT`: no valid binding;
- `READY`: bound, with no tracked failure since the latest tracked success;
- `DEGRADED`: bound, with a tracked transport failure since the latest tracked
  success.

Health counters and timestamps are observational. They never suppress a
requested transfer. `probe()` is a diagnostic raw transfer and does not update
health. The caller decides absence policy, retry eligibility, device health,
and bus recovery.

The class is non-copyable, non-movable, single-threaded, non-reentrant, and not
ISR-safe. Keep it in stable storage and serialize every call through one owner.

## Interrupt errata

`readInputs()`, `clearInterrupts()`, and cooperative input service read both
input ports and then write the nonzero safe command byte used by the TI errata
workaround. If a failed receive leaves the command phase accepted or uncertain,
they still attempt the park once and preserve the read error as primary. A
proven not-attempted command does not need cleanup. These full-pair APIs are the
recommended INT service path. Scalar `readInput()`/`readPin()` calls read only
the selected port, then apply the same park rule; they do not service pending
changes on the other port. The owner must keep each read/park sequence exclusive.
A park failure remains observable even when input data was read successfully.

`applyInterruptErrataWorkaround()` is an advanced one-transfer diagnostic. It
does not replace owner-exclusive input service.

## Migration from 2.x

Version 3.0.0 is a breaking release:

- transport callbacks return `TransportResult`, not `Status`;
- `bind()`/`begin()` and `detach()`/`end()` are passive and return `Status`;
- presence and POR-default checks are explicit `probe()` and
  `checkPorDefaults()` calls;
- `DriverState::OFFLINE` is removed from the state machine; its error value is
  reserved only for source compatibility and v3 never gates I2C;
- lock hooks, offline thresholds, implicit reset-default checks, and cached
  desired configuration were removed from `Config`;
- typed `Pin`, `Port`, `Level`, and `Direction` values are the canonical API;
  narrow bool compatibility overloads remain where they preserve the same
  validation and hardware behavior;
- apply, verify, and input-read compounds use exact-ID cooperative operations;
- the v2 job scheduler, `tick()`, `recover()`, duplicate unlocked errata alias,
  last-job/input accessors, and ambiguous `isOnline()` helper were removed.
  Use the exact-ID operation API, returned input/result evidence, `isBound()`,
  explicit diagnostics, and caller-owned recovery policy;
- there is no rare-operation, maintenance, NVM, or calibration API because the
  PCA9555 has none of those chip functions.

## Examples and validation

- `examples/01_basic_bringup_cli` is the Arduino bring-up and HIL CLI.
- `examples/espidf_basic` is a native ESP-IDF example with no Arduino facade.
- `examples/common` is example-only glue, not library API or a production bus
  manager.

The CLI raw-write commands `wreg` and `wregs` accept only Output and Polarity
register starts `2` through `5`. Configuration registers `6` and `7` are
intentionally rejected; use the named direction commands so output latches are
preloaded safely before pins become outputs.

The `recover` CLI spelling is retained for operator familiarity, but it now
means: apply the example's explicit image of high output latches, normal
polarity, and all pins input. It is application policy, not a library bus
recovery API.

## Documentation

The documentation index at `docs/README.md` separates public API guidance,
chip/register facts, hardware evidence, and remaining validation work. Public
API details are maintained in the headers under `include/PCA9555/`.

Generate the local HTML reference with:

```bash
doxygen Doxyfile
```

Open `docs/doxygen/html/index.html` after generation. Generated HTML is ignored
by Git and is not shipped in the library package. Doxygen treats undocumented
public symbols and documentation errors as failures; internal example helpers
are intentionally outside the generated API surface.

See the [hardware validation runbook](docs/hardware_validation.md) for the
evidence still required on real hardware and the limits on field-readiness
claims.

## License

MIT. See `LICENSE`.

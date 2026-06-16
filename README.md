# PCA9555 Driver Library

Production-oriented, framework-neutral PCA9555 16-bit I/O expander I2C driver.
The core has been hardened for portability, dirty-state diagnostics, safe
runtime output enabling, interrupt/errata handling, fault-injection tests,
reproducible Arduino builds, and ESP-IDF component/example packaging. It remains
a pre-production candidate until the hardware validation matrix is run and
recorded on real target boards.

Release evidence:

- [Documentation index](docs/README.md)
- [Release checklist](docs/release.md)
- [Hardware validation and HIL runbook](docs/hardware_validation.md)

## Features

- **Injected I2C transport** - no Wire dependency in library code
- **Health monitoring** - automatic state tracking (READY/DEGRADED/OFFLINE)
- **Deterministic behavior** - no unbounded loops, no heap allocations
- **Managed synchronous lifecycle** - blocking I2C ops with clean begin/tick/end
- **Chunked jobs** - caller-budgeted input, output, and safe configuration jobs via `pollJob()`
- **Settings snapshot** - access the active runtime config and health counters with `getSettings()`
- **16-bit I/O** - two independent 8-bit ports (Port 0 and Port 1)
- **Interrupt errata workaround** - configurable automatic errata mitigation
- **Single-pin helpers** - pin-level readback plus atomic read-modify-write for output, direction, and polarity
- **Bit manipulation helpers** - 16-bit mask-based set/clear/toggle for outputs, direction, and polarity in a single I2C burst
- **Bulk register helpers** - pair-bounded `readRegisters()` / `writeRegisters()` for low-level diagnostics
- **Recoverable runtime state** - `recover()` reapplies the latest live output/config/polarity state
- **Cache-safe writes** - output, direction, and polarity mirrors update only after successful I2C writes
- **Glitch-safe output enabling** - runtime output direction changes preload latches before enabling drivers

## Safety and Electrical Notes

- The core library does not own I2C and never initializes bus pins or clocks; the
  application-provided transport owns all bus setup and serialization.
- PCA9555 outputs are push-pull. Verify external loads before enabling outputs;
  the device is rated for limited source/sink current and is not a power driver.
- Use `configureOutputs(mask, values)` or `preloadOutput()` before enabling
  outputs at runtime so the output latch is written before the direction bit is
  cleared.
- Input Port registers are pin-dependent sense values, not fixed reset
  constants. Output Port registers are latches and do not prove physical pin
  voltage.
- INT is active-low/open-drain and requires a pull-up. I2C APIs are not ISR-safe;
  service INT by notifying task/main context and then reading input ports.
- Output-to-input direction changes can cause false interrupt behavior. Treat
  the first post-change interrupt as a rebaseline event.
- `probe()` only proves that an address responded on the bus. The PCA9555 has no
  chip-ID register, and `begin()` default checks are plausibility checks rather
  than identity proof.
- `recover()` reapplies cached desired state after communication faults; it
  cannot force a true PCA9555 power-on reset.

## Installation

### PlatformIO (recommended)

Add to `platformio.ini`:

```ini
lib_deps = 
  https://github.com/janhavelka/PCA9555.git
```

### Manual

Copy `include/PCA9555/` and `src/` to your project.

### ESP-IDF Component

This repository also builds as a pure ESP-IDF component. Add the repo as an
extra component or dependency, then include `PCA9555/PCA9555.h` and provide
`Config::i2cWrite` / `Config::i2cWriteRead` callbacks from your project-owned
I2C master bus.

The ESP-IDF bring-up CLI is implemented as a native IDF example with the same
command contract as the Arduino CLI:

```bash
cd examples/espidf_basic
idf.py set-target esp32s3
idf.py build
```

The ESP-IDF example uses `app_main`, `driver/i2c_master.h`, `esp_timer`,
`vTaskDelay`, and fixed C command buffers. It does not include Arduino CLI
sources or compatibility facades.

Mutating ESP-IDF CLI commands require a final `confirm` token. Without it, the
example prints what would change, why confirmation is required, and the exact
confirmed command form. Validation status: command parity is checked by
repo-local contract scripts. ESP-IDF hardware smoke tests and output-driving
validation remain pending until target devices are available.

## Quick Start

```cpp
#include <Wire.h>
#include "PCA9555/PCA9555.h"

PCA9555::Status myI2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                           uint32_t timeoutMs, void* user);
PCA9555::Status myI2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                               uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                               void* user);

uint32_t myNowMs(void* user) {
  (void)user;
  return millis();
}

PCA9555::PCA9555 device;

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9, 400000);
  
  PCA9555::Config cfg;
  cfg.i2cWrite = myI2cWrite;
  cfg.i2cWriteRead = myI2cWriteRead;
  cfg.i2cUser = &Wire;
  cfg.nowMs = myNowMs;
  cfg.i2cAddress = 0x20;
  cfg.configPort0 = 0x0F;   // Port 0 lower nibble = input, upper = output
  cfg.outputPort0 = 0xF0;   // Set Port 0 output latch bits high initially
  
  auto status = device.begin(cfg);
  if (!status.ok()) {
    Serial.printf("Init failed: %s\n", status.msg);
    return;
  }
  
  Serial.println("PCA9555 initialized!");
}

void loop() {
  device.tick(millis());
  
  // Read all inputs
  PCA9555::PortData inputs;
  if (device.readInputs(inputs).ok()) {
    Serial.printf("Inputs: P0=0x%02X P1=0x%02X\n", inputs.port0, inputs.port1);
  }
  
  // Write a single output latch bit. Linear pin 12 maps to physical P14.
  device.writePin(12, true);
  
  delay(1000);
}
```

Provide your own `Config::i2cWrite` and `Config::i2cWriteRead` callbacks, or copy/adapt the
ready-made Arduino helper from `examples/common/I2cTransport.h`. That helper is example-only
and not part of the installed library package. Applications that need meaningful health
timestamps should inject `Config::nowMs`; otherwise timestamps remain `0`.

## Health Monitoring

The driver tracks I2C communication health:

```cpp
// Check state
if (device.state() == PCA9555::DriverState::OFFLINE) {
  Serial.println("Device offline!");
  device.recover();  // Try to reconnect
}

// Get statistics
Serial.printf("Failures: %u consecutive, %lu total\n",
              device.consecutiveFailures(), device.totalFailures());
```

### Driver States

| State | Description |
|-------|-------------|
| `UNINIT` | `begin()` not called or `end()` called |
| `READY` | Operational, no recent failures |
| `DEGRADED` | 1+ failures, below offline threshold |
| `OFFLINE` | Too many consecutive failures |

## Terminology

- **Port**: an 8-bit register bank. Port 0 maps to `P00-P07`; Port 1 maps to `P10-P17`.
- **Pin**: a linear pin index `0-15`. Pins `0-7` are Port 0; pins `8-15` are Port 1. Example: pin `12` is Port 1, bit 4 (`P14`).
- **Polarity**: input inversion only. `0 = normal`, `1 = inverted`. It changes how input reads are reported; it does not invert the output driver.
- **Mask**: a 16-bit bitmap used by the bit-manipulation helpers and CLI. Bit `0 = P00`, bit `7 = P07`, bit `8 = P10`, bit `15 = P17`. Example: `0x0103` selects `P00`, `P01`, and `P10`.

## Migration Notes

`PCA9555::PCA9555` instances are intentionally non-copyable and non-movable.
Keep each driver instance in stable storage for its lifetime, such as a global,
static, owning object member, or application-managed pointer, and pass it by
reference or pointer. Do not return driver instances by value, store them in
containers that relocate elements, or copy them between tasks. This avoids
duplicating cached hardware state, health counters, and transport callback
context.

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - Initialize driver, verify device, apply configuration
- `void tick(uint32_t nowMs)` - Advance one pending chunked job instruction, if a job is active
- `void end()` - Shutdown driver and set pins to input when online; if already `OFFLINE`, it clears local state without extra I2C

### Chunked Job API

Chunked jobs are for callers that need to limit backend I2C work per owner poll.
One register-pair read or write is one instruction. The interrupt errata
pointer-park write is also one instruction. `pollJob(nowMs, maxInstructions)`
runs at most that many instructions and returns `IN_PROGRESS` while work
remains.

- `Status startReadInputsJob()` - Schedule an input register-pair read; with errata enabled, pointer parking is a later instruction unless budget allows both
- `Status startWriteOutputsJob(uint16_t mask, uint16_t value)` - Schedule one masked output latch pair write; CPU-only no-op if the cached latch already matches
- `Status startConfigureOutputsJob(uint16_t mask, uint16_t value)` - Schedule safe output enabling: preload selected latch bits, then write configuration bits
- `Status pollJob(uint32_t nowMs, uint8_t maxInstructions)` - Execute up to the requested instruction budget
- `bool jobActive() const` - True while a chunked job is active
- `Status lastJobStatus() const` - Last completed or failed job status
- `Status getLastReadInputs(PortData& data) const` - Last completed full input-pair read result

### Diagnostics

- `Status probe()` - Check device presence via raw I2C (no health tracking);
  transport failures return `DEVICE_NOT_FOUND` with the original detail code preserved
- `Status recover()` - Attempt recovery with health tracking + re-apply the current runtime config; clears dirty state only after full success

### Input API

- `Status readInputs(PortData& data)` - Read both input ports after configured polarity inversion; clears both port interrupt sources
- `Status readInputsAndClearInterrupt(uint16_t& value)` - Preferred task/main-context read after INT notification; clears both port interrupt sources and returns combined `P17..P00`
- `Status clearInterrupts()` - Clear both port interrupt sources by reading both input ports
- `Status applyInterruptErrataWorkaround()` - Park the command pointer at `cmd::ERRATA_SAFE_CMD` (`0x02`) under the optional lock hooks
- `Status applyInterruptErrataWorkaroundUnlocked()` - One-instruction pointer park without acquiring optional lock hooks
- `Status readInput(Port port, uint8_t& value)` - Read one input port after configured polarity inversion; clears only that port's interrupt source
- `Status readPin(Pin pin, bool& state)` - Read one input-register bit after configured polarity inversion; clears only the containing port's interrupt source

### Output API

- `Status writeOutputs(const PortData& data)` - Write both output latch registers
- `Status writeOutput(Port port, uint8_t value)` - Write one output latch register
- `Status readOutput(Port port, uint8_t& value)` - Read single output latch register
- `Status writePin(Pin pin, bool high)` - Write a single output latch bit (uses cached value)
- `Status readOutputPin(Pin pin, bool& high)` - Read single output latch bit
- `Status readOutputs(PortData& data)` - Read back output latch register values
- `Status preloadOutput(Pin pin, bool high)` - Force-write one latch bit without changing direction
- `Status preloadOutputs(uint16_t mask, uint16_t values)` - Force-write selected latch bits without changing directions

### Configuration API

- `Status setConfiguration(const PortData& data)` - Set pin directions (1=input, 0=output); input-to-output bits preload cached latch values first
- `Status setPortConfiguration(Port port, uint8_t value)` - Set single port direction; input-to-output bits preload cached latch values first
- `Status getPortConfiguration(Port port, uint8_t& value)` - Read single port direction
- `Status getConfiguration(PortData& data)` - Read pin direction configuration
- `Status setPolarity(const PortData& data)` - Set polarity inversion
- `Status setPortPolarity(Port port, uint8_t value)` - Set single port polarity
- `Status getPortPolarity(Port port, uint8_t& value)` - Read single port polarity
- `Status getPolarity(PortData& data)` - Read polarity inversion
- `Status setPinPolarity(Pin pin, bool inverted)` - Set single pin polarity (uses cached value)
- `Status getPinPolarity(Pin pin, bool& inverted)` - Read single pin polarity
- `Status setPinDirection(Pin pin, bool input)` - Legacy bool direction helper; output transitions preload the cached latch value
- `Status setDirection(Pin pin, Direction direction)` - Explicit direction helper using `Direction::INPUT_MODE` / `Direction::OUTPUT_MODE`
- `Status configureOutputs(uint16_t outputMask, uint16_t outputValues)` - Preferred bulk API for enabling outputs with explicit initial latch values
- `Status getPinDirection(Pin pin, bool& input)` - Read single pin direction

### Bit Manipulation API

Mask-based helpers use a 16-bit mask (bit 0 = P00, bit 15 = P17) and cached
shadow registers. Paired output, input-direction, and polarity mask helpers
write both ports in a single 2-byte I2C burst when their mask causes a change.
`togglePin()` writes one output-latch byte, and legacy `configureOutputBits()`
may perform a preload write before the configuration write. If a write fails,
the cached shadow state remains unchanged so later single-pin and recovery
operations do not build on a failed update. The affected hardware state is
marked dirty because the device may still have accepted part of the transaction.

- `Status setOutputBits(uint16_t mask)` - Set output latch bits HIGH (OR mask into shadow)
- `Status clearOutputBits(uint16_t mask)` - Clear output latch bits LOW (AND ~mask into shadow)
- `Status toggleOutputBits(uint16_t mask)` - Toggle output latch bits (XOR mask into shadow)
- `Status togglePin(Pin pin)` - Toggle a single output latch bit (1-byte write, no read)
- `Status configureInputBits(uint16_t mask)` - Set masked pins to INPUT direction; output-to-input changes may trigger PCA9555 interrupt behavior
- `Status configureOutputBits(uint16_t mask)` - Legacy OUTPUT helper using cached latch values; prefer `configureOutputs()` when the initial level matters
- `Status setInvertBits(uint16_t mask)` - Enable polarity inversion for masked pins
- `Status clearInvertBits(uint16_t mask)` - Disable polarity inversion for masked pins

### Register Access

- `Status readRegister(uint8_t reg, uint8_t& value)` - Read any register (0-7)
- `Status writeRegister(uint8_t reg, uint8_t value)` - Write writable register (2-7)
- `Status readRegisters(uint8_t startReg, uint8_t* buf, size_t len)` - Read 1-2 registers in one auto-increment pair; an odd start wraps to the pair mate
- `Status writeRegisters(uint8_t startReg, const uint8_t* buf, size_t len)` - Write 1-2 registers in one auto-increment pair; an odd start wraps to the pair mate

Direct register writes are diagnostic tools. A successful direct write updates the matching
runtime cache, but a failed direct write marks hardware state dirty because the device may
have accepted one register byte while the cache remained unchanged. Use `recover()` to
reapply the cached desired state after a dirty direct write.

Direct reads of Input Port registers have the same interrupt side effects as the typed input
APIs: the read port's interrupt state is cleared, and the errata pointer-park write is
performed when enabled. If an input-register read succeeds but pointer parking fails, the
destination buffer contains valid input data and the failing errata `Status` is returned.

### Adapter Surface Guidance

For TunnelMonitor-style integrations, wrap only the small stable subset needed
by the application: 16-bit masks, full input reads, masked output writes,
polarity/configuration masks, and recovery/dirty-state diagnostics. Leave raw
register access, single-pin convenience helpers, and direct errata primitives as
library-level tools unless the application has a concrete need for them.

### Health

- `DriverState state()` - Current driver state
- `DriverState driverState()` - Cross-library state alias
- `bool isInitialized()` - True after `begin()` succeeds and before `end()`
- `bool isOnline()` - True if READY or DEGRADED
- `const Config& getConfig()` - Current recoverable runtime configuration snapshot
- `SettingsSnapshot getSettings()` - Combined settings plus health snapshot
- `Status getSettings(SettingsSnapshot& out)` - Status-returning snapshot copy for uniform callers
- `uint32_t lastOkMs()` / `lastErrorMs()` - Timestamps
- `Status lastError()` - Most recent error
- `uint8_t consecutiveFailures()` - Failures since last success
- `uint32_t totalFailures()` / `totalSuccess()` - Lifetime counters
- `bool hardwareStateDirty()` - True when writable hardware registers may differ from cached desired state after a failed write
- `Status hardwareStateDirtyError()` - Original transport error that marked hardware state dirty

## Configuration

### Config Fields

| Field | Default | Description |
|-------|---------|-------------|
| `i2cAddress` | `0x20` | I2C address (0x20-0x27) |
| `i2cTimeoutMs` | `50` | I2C transaction timeout |
| `offlineThreshold` | `5` | Consecutive failures before OFFLINE |
| `configPort0/1` | `0xFF` | Pin direction (1=input, 0=output) |
| `outputPort0/1` | `0xFF` | Initial output latch values |
| `polarityPort0/1` | `0x00` | Polarity inversion (1=inverted) |
| `requireConfigPortDefaults` | `true` | Require Configuration Port 0/1 = `0xFF` at `begin()` |
| `applyInterruptErrata` | `true` | Enable interrupt errata workaround |
| `i2cLock` / `i2cUnlock` | `nullptr` | Optional shared-bus lock hooks for compound input-read plus errata-write sequences |
| `lockUser` | `nullptr` | User context for lock hooks |

Failed `begin()` calls clear stale runtime/cache state before returning. This
prevents a later diagnostic snapshot from reporting old output, polarity, or
configuration shadows after an unsuccessful initialization attempt.
If either `i2cLock` or `i2cUnlock` is set, both must be set or `begin()` returns
`INVALID_CONFIG`. A non-OK lock result means no lock was acquired and no unlock callback
is called.

## Runtime Direction Safety

The PCA9555 keeps input sensing, output latch state, and direction control in
separate registers:

- **Input Port** registers report sampled pin sense after configured polarity inversion.
- **Output Port** registers store latch bits; reading them returns the flip-flop, not pin voltage.
- **Configuration** registers select direction (`1=input`, `0=output`). A pin drives the latch only after its configuration bit is cleared.

To avoid load glitches, the driver writes the desired output latch before any
input-to-output transition. `configureOutputs(mask, values)` is the preferred
runtime API when enabling one or more outputs because it records the intended
initial latch values at the same call site as the direction change. For a single
pin, call `preloadOutput(pin, level)` followed by `setDirection(pin,
Direction::OUTPUT_MODE)` when you want the level to be explicit.

`preloadOutput()` and `preloadOutputs()` force an I2C write even when the cached
latch already matches the requested value. This is intentional after external
mutation, reset, diagnostics, or dirty-state recovery. The legacy direction APIs
remain available: `setConfiguration()`, `setPortConfiguration()`,
`setPinDirection(pin, false)`, and `configureOutputBits()` now preload cached
output latch values before clearing direction bits. If the desired level must be
obvious to reviewers, use `configureOutputs()` instead of relying on cache state.

If a preload write fails, the driver returns that error and does not attempt the
direction write. If preload succeeds but the configuration write fails, the
original configuration-write error is returned and `hardwareStateDirty()` is set;
the output latch may have been updated while the direction bits may or may not
have changed.

Output-to-input transitions write only the configuration register. They can
cause PCA9555 false-interrupt behavior if the sampled input state differs from
the previous input-register state. Treat the first interrupt after such a change
as a rebaseline event and read the input ports from normal task or loop context.

Startup and recovery preserve the safe application order:
output latch -> polarity -> configuration -> input read -> optional errata workaround.

### I2C Address Selection

| A2 | A1 | A0 | Address |
|----|----|----|---------|
| L | L | L | `0x20` |
| L | L | H | `0x21` |
| L | H | L | `0x22` |
| L | H | H | `0x23` |
| H | L | L | `0x24` |
| H | L | H | `0x25` |
| H | H | L | `0x26` |
| H | H | H | `0x27` |

Up to 8 devices can share one bus. The address pins must be tied high or low and must not float.

## Hardware Dirty State

I2C writes cannot be treated as atomic at the driver boundary. A transport callback can return
`I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, or another failure after the PCA9555 has already
accepted one data byte from a single-register or paired-register write. In that case the API
returns the original transport `Status` unchanged, leaves the cached desired state unchanged,
and sets `hardwareStateDirty()`.

Dirty state means hardware may no longer match the driver's cached desired output,
configuration, or polarity registers. It is diagnostic state, not a replacement error code.
Inspect it with `hardwareStateDirty()`, `hardwareStateDirtyError()`, or `getSettings()`.
`hardwareStateDirtyError()` preserves the original write failure, including specific transport
codes such as `I2C_NACK_DATA`, `I2C_TIMEOUT`, and `I2C_BUS`.

Validation errors, `NOT_INITIALIZED`, `BUSY`, failed reads, and `IN_PROGRESS` statuses do not
mark hardware dirty. Because the current transport contract has no explicit "no byte reached
the device" signal, failed data writes are marked dirty conservatively even if a platform
transport failed before the fake hardware model applied a byte.

`recover()` is the reconciliation path. It re-reads a configuration register,
reapplies the cached desired output, polarity, and configuration registers, clears pending
interrupts, and applies the interrupt errata workaround. Dirty state clears only after that
full sequence succeeds. A failed or partially successful `recover()` leaves dirty state set.
A later successful `begin()` also clears dirty state after it verifies the device and applies
the requested configuration; failed `begin()` validation preserves any existing dirty state.

## Behavioral Contracts

1. **Threading model**: `PCA9555` instances are single-threaded and non-reentrant. If multiple tasks can access one instance, the application must serialize those calls externally.
2. **ISR safety**: Public APIs that touch I2C are not ISR-safe. Methods may update driver state and may call blocking transport callbacks. From an INT pin interrupt, set a flag or notify a task, then call `readInputsAndClearInterrupt()` or `clearInterrupts()` from task/main context.
3. **Timing model**: `tick(nowMs)` is bounded and advances at most one active chunked-job instruction. `pollJob(nowMs, maxInstructions)` is bounded by the caller-supplied instruction budget. `Config::nowMs` is optional and used for normal health timestamps; job-driven I2C uses the `nowMs` supplied to `tick()` or `pollJob()`. If absent outside job polling, health timestamps remain `0`.
4. **Transport callbacks**: `Config::i2cWrite` and `Config::i2cWriteRead` must complete synchronously before returning, must not retain buffer pointers after return, and must not recursively call back into the same `PCA9555` instance.
5. **External bus serialization**: The library does not own or initialize the I2C bus. The application transport must serialize shared-bus access across tasks, driver instances, and other I2C clients. Optional `Config::i2cLock` / `Config::i2cUnlock` hooks serialize synchronous compound input-read plus errata-write sequences and the locked public errata helper; callbacks invoked while this compound lock is held must not reacquire the same non-recursive lock. Chunked jobs execute one backend transfer per instruction and do not retain optional locks across polls, so shared-bus owners must serialize chunked job polls at the owner level.
6. **Resource ownership**: Bus and pins are provided by the application via `Config`.
7. **Memory behavior**: The library performs no dynamic allocation in `begin()` or steady state.
8. **Error handling**: All fallible APIs return `Status`. Silent failure is not possible.
9. **Health behavior**: transport `IN_PROGRESS` statuses are passed through without
   incrementing success or failure counters. `OFFLINE` is latched; normal public
   I2C operations return `BUSY` with `Driver is offline; call recover()` without
   touching the bus until `recover()` succeeds.

`begin()` verifies presence by reading both Configuration Port registers. By default it requires the
POR default `0xFF/0xFF` state and returns `CONFIG_REG_MISMATCH` if the expander is already
configured. If your MCU can reset without power-cycling the PCA9555, set
`Config::requireConfigPortDefaults = false` so `begin()` will accept the existing device state and
immediately apply the requested runtime configuration.

## Interrupt Errata

The PCA9555 INT output is active-low/open-drain and requires a pull-up. Input-port reads
clear interrupt state for the corresponding port: `readInputs()`,
`readInputsAndClearInterrupt()`, and `clearInterrupts()` read both ports, while
`readInput(port)` and `readPin(pin)` clear only the selected/containing port. Output pins
do not generate input-change interrupts. Output-to-input direction changes can cause false
interrupts if the sampled input state differs from the previous input-register state.

For INT service, keep the GPIO ISR minimal: set a flag or notify a task, then call
`readInputsAndClearInterrupt()` or `clearInterrupts()` outside the ISR. I2C driver APIs are
not ISR-safe.

The PCA9555 has a known issue: after reading input ports, the internal register pointer
sits at address `0x00`. If another device on the same I2C bus is subsequently read, the
PCA9555 may incorrectly interpret the address byte as a register write, potentially
clearing the interrupt output. Setting `Config::applyInterruptErrata = true` (default)
causes the driver to write the safe nonzero command byte `cmd::ERRATA_SAFE_CMD` (`0x02`)
after every input read to move the register pointer away from `0x00`. If the input read
succeeds but this pointer-park write fails, the returned input data is valid but the API
returns the errata write error; the failure also updates driver health.

For synchronous input APIs, optional `i2cLock` / `i2cUnlock` hooks make the driver hold
the lock across the input read and the errata pointer-park write, releasing it on success,
read failure, or errata write failure. The public `applyInterruptErrataWorkaround()` helper
also uses these hooks; `applyInterruptErrataWorkaroundUnlocked()` is the explicit
one-transfer primitive for callers that already own the bus.

Chunked input jobs intentionally split this sequence into budgeted instructions:
read input pair first, then pointer-park on a later `pollJob()` call unless the
caller supplies a budget of at least two. Chunked jobs do not acquire or retain
optional locks, so bus-owner code must prevent unrelated transfers between
those polls when the errata workaround matters on a shared bus.

## Device Notes

- Reading the **Output Port** register returns the output latch (flip-flop), not the actual pin voltage.
- Reading the **Input Port** register reports the input-register sense after configured polarity inversion. With normal polarity this corresponds to the physical pin level, including when the pin is configured as an output.
- Only pins configured as inputs generate input-change interrupts.
- Each PCA9555 I/O has an internal ~100 kOhm pull-up when configured as an input. Inputs held low draw extra standby current, so unused inputs should be left high or configured as outputs driven high in low-power designs.

## Examples

### 01_basic_bringup_cli

Interactive serial CLI for device bringup and diagnostic testing. It covers bus scan,
driver health, settings snapshots, input/output readback, output latch writes,
direction and polarity configuration, direct register access, exact 16-bit
patterns, mask-based bit manipulation, self-test, and stress diagnostics.

Run `help` on the serial console for the complete command list. Diagnostic
output uses physical PCA9555 labels (`P00-P07`, `P10-P17`) while command
arguments keep the driver API's linear pin numbering (`0-15`). Stress progress
lines are intentionally plain except for the `ok=` and `fail=` result counts.
In the native ESP-IDF CLI, output-driving, direction, polarity, raw write,
pattern, recovery, self-test, sweep/walk, and stress commands require a final
`confirm` token.

Typical bring-up commands:

```text
scan
cfg
pattern 0x00FF confirm
read input port 0
read output port 1
pininfo 12
pins
```

### espidf_basic

Native ESP-IDF build of the bring-up CLI command contract. It uses `app_main`,
`driver/i2c_master.h`, `esp_timer`, `vTaskDelay`, and fixed C buffers. The
command set, output wording, health diagnostics, self-test, stress commands,
sweep, walk, and pattern flows stay aligned with the Arduino CLI contract.
`tools/check_idf_example_contract.py` rejects Arduino compatibility facades and
checks the native IDF command surface, persistent device handle, and explicit
confirmation guard. ESP-IDF hardware validation is pending; do not treat the
native example as hardware-proven for output-driving workflows until that smoke
test is completed.

### Example Helpers (`examples/common/`)

| File | Purpose |
|------|---------|
| `BuildConfig.h` | Compile-time log level and core debug level |
| `Log.h` | Colorized serial logging macros (`LOGI`, `LOGE`, `LOGV`, etc.) |
| `BoardConfig.h` | Board-specific pin definitions and I2C init |
| `I2cTransport.h` | Wire-based transport adapter mapping to `Status` |
| `TransportAdapter.h` | Alias wrapper matching the standardized helper layout |
| `I2cScanner.h` | I2C bus scanner utility |
| `CommandHandler.h` | Serial command-line helpers |
| `CliShell.h` | Serial line reader helper for CLI examples |
| `BusDiag.h` | Bus diagnostic scan wrapper |
| `HealthDiag.h` | Driver health snapshot printer |
| `HealthView.h` | Compact one-line health summary helper |

These helpers are **not** part of the library - they exist only to keep examples self-contained.
Examples are diagnostic/bring-up code unless they explicitly add production bus management,
scheduling, and hardware fault handling for the target application.

Library diagnostics are limited to `probe()`, `recover()`, `getSettings()`, health accessors,
and direct register access. Serial CLI commands, scanners, stress tests, and helpers under
`examples/common/` are example-only project glue and are not part of the public library API.

## Build Matrix

Locally validated and release-gate build inputs for this hardening branch:

| Target | Command | Environment | Status / Assumptions |
|---|---|---|---|
| Native tests | `python -m platformio test -e native` | PlatformIO Core 6.1.19, Unity native env | Local validation gate; host GCC; no Arduino/Wire in core |
| ESP32-S2 Arduino compile | `python -m platformio run -e esp32s2dev` | Espressif 32 platform 54.3.20, Arduino core 3.2.0, IDF libs 5.4.0 | Local validation gate; `esp32-s2-saola-1`, 4 MB flash-compatible config, USB CDC on boot |
| ESP32-S3 Arduino compile | `python -m platformio run -e esp32s3dev` | Espressif 32 platform 54.3.20, Arduino core 3.2.0, IDF libs 5.4.0 | Local validation gate; `esp32-s3-devkitc-1`, 4 MB flash-compatible config, no PSRAM dependency |
| ESP-IDF example guard | `python tools/check_idf_example_contract.py` | Python 3 | Local static contract gate for `examples/espidf_basic` |
| ESP-IDF example build | `idf.py -C examples/espidf_basic set-target esp32s3 build` | ESP-IDF v5.4 line | Release/CI gate; not locally validated unless `idf.py` or CI logs are available |
| ESP-IDF example build | `idf.py -C examples/espidf_basic set-target esp32s2 build` | ESP-IDF v5.4 line | Release/CI gate; not locally validated unless `idf.py` or CI logs are available |

These are compile/test gates unless a report explicitly states hardware was connected
and exercised. Local pure ESP-IDF builds require `idf.py` on PATH.

## Running Tests

```bash
# Build for ESP32-S3 (verifies compilation)
python -m platformio run -e esp32s3dev

# Build for ESP32-S2
python -m platformio run -e esp32s2dev

# Run native unit tests (requires host GCC)
python -m platformio test -e native

# Build the ESP-IDF full CLI example (requires ESP-IDF on PATH)
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build

# Check repo-standardized CLI/helper and core framework contracts
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py

# Check the pure ESP-IDF example contract
python tools/check_idf_example_contract.py

# Check host-side I2C HIL runner/docs contract
python tools/check_hil_contract.py

# Plan a HIL run without opening serial or claiming hardware validation
python tools/run_i2c_hil.py --dry-run
```

## Documentation

- [CHANGELOG](CHANGELOG.md)
- [Documentation Index](docs/README.md)
- [Release Checklist](docs/release.md)
- [Hardware Validation And HIL Runbook](docs/hardware_validation.md)
- [PCA9555 Implementation Manual](PCA9555_io_expander_implementation_manual.md)
- [ESP-IDF Notes](docs/espidf.md)
- [Register Reference](docs/register_reference.md)
- [TI PCA9555 Datasheet](https://www.ti.com/lit/ds/symlink/pca9555.pdf)
- [Contributing Guide](CONTRIBUTING.md)
- `Doxyfile` indexes public headers, the docs folder, the Arduino CLI,
  and the native IDF entry point.

## License

MIT License - see [LICENSE](LICENSE) for details.

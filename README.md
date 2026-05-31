# PCA9555 Driver Library

Production-oriented, framework-neutral PCA9555 16-bit I/O expander I2C driver.
The core has been hardened for portability, dirty-state diagnostics, safe
runtime output enabling, interrupt/errata handling, fault-injection tests,
reproducible Arduino builds, and ESP-IDF component/example packaging. It remains
a pre-production candidate until the hardware validation matrix is run and
recorded on real target boards.

Baseline and release evidence:

- [Pre-hardening industry readiness audit baseline](docs/PCA9555_INDUSTRY_READINESS_AUDIT.md)
- [Release checklist](docs/PCA9555_RELEASE_CHECKLIST.md)
- [Hardware validation matrix](docs/PCA9555_HARDWARE_VALIDATION_MATRIX.md)
- [I2C HIL runbook](docs/I2C_HIL_RUNBOOK.md)
- [I2C HIL target template](docs/I2C_HIL_TARGET_TEMPLATE.md)
- [I2C HIL self-test report](docs/I2C_HIL_SELFTEST_REPORT.md)
- [Prompt 07 documentation/release-gates report](docs/PCA9555_HARDENING_PROMPT_07_DOCS_RELEASE_GATES_REPORT.md)

## Features

- **Injected I2C transport** - no Wire dependency in library code
- **Health monitoring** - automatic state tracking (READY/DEGRADED/OFFLINE)
- **Deterministic behavior** - no unbounded loops, no heap allocations
- **Managed synchronous lifecycle** - blocking I2C ops with clean begin/tick/end
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
and not part of the installed library package. `Config::nowMs` is optional and used for
diagnostic timestamps; if it is not supplied, health timestamps remain `0`.

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
- `void tick(uint32_t nowMs)` - Process pending operations (currently no-op, reserved)
- `void end()` - Shutdown driver and set pins to input when online; if already `OFFLINE`, it clears local state without extra I2C

### Diagnostics

- `Status probe()` - Check device presence via raw I2C (no health tracking);
  transport failures return `DEVICE_NOT_FOUND` with the original detail code preserved
- `Status recover()` - Attempt recovery with health tracking + re-apply the current runtime config; clears dirty state only after full success

### Input API

- `Status readInputs(PortData& data)` - Read both input ports after configured polarity inversion; clears both port interrupt sources
- `Status readInputsAndClearInterrupt(uint16_t& value)` - Preferred task/main-context read after INT notification; clears both port interrupt sources and returns combined `P17..P00`
- `Status clearInterrupts()` - Clear both port interrupt sources by reading both input ports
- `Status applyInterruptErrataWorkaround()` - Park the command pointer at `cmd::ERRATA_SAFE_CMD` (`0x02`)
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
3. **Timing model**: `tick(nowMs)` is bounded and currently performs no I2C. `Config::nowMs` is optional and used only for health timestamps unless a specific API documents otherwise. If supplied, use monotonic `uint32_t` milliseconds in the same clock domain as `tick(nowMs)`; wrap at `UINT32_MAX` is allowed. If absent, health timestamps remain `0`.
4. **Transport callbacks**: `Config::i2cWrite` and `Config::i2cWriteRead` must complete synchronously before returning, must not retain buffer pointers after return, and must not recursively call back into the same `PCA9555` instance.
5. **External bus serialization**: The library does not own or initialize the I2C bus. The application transport must serialize shared-bus access across tasks, driver instances, and other I2C clients. Optional `Config::i2cLock` / `Config::i2cUnlock` hooks serialize the compound input-read plus errata-write sequence; callbacks invoked while this compound lock is held must not reacquire the same non-recursive lock. If hooks are absent, the external bus manager must prevent another transaction from running between the input read and the follow-up safe command write.
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

When optional `i2cLock` / `i2cUnlock` hooks are configured, the driver holds the lock
across the input read and the errata pointer-park write and releases it on success, read
failure, or errata write failure. If a lock callback fails, the input read is not attempted.
When hooks are absent, shared-bus applications must provide equivalent serialization in
their bus manager.

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

Typical bring-up commands:

```text
scan
cfg
pattern 0x00FF
read input port 0
read output port 1
pininfo 12
pins
```

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

### ESP-IDF Basic Example

`examples/esp_idf/basic/` is a pure ESP-IDF diagnostic example using `app_main()`.
It owns I2C bus initialization, adapts native ESP-IDF I2C transactions to
`PCA9555::Status`, passes timeout values through the transport callbacks, and uses
a recursive FreeRTOS mutex for both per-transaction access and the optional
compound input-read/errata lock hooks.

The example does not include Arduino or `Wire`, and it does not claim production
shared-bus completeness or hardware validation. Review the default GPIOs before
connecting hardware.

## Build Matrix

Locally validated and release-gate build inputs for this hardening branch:

| Target | Command | Environment | Status / Assumptions |
|---|---|---|---|
| Native tests | `python -m platformio test -e native` | PlatformIO Core 6.1.19, Unity native env | Local validation gate; host GCC; no Arduino/Wire in core |
| ESP32-S2 Arduino compile | `python -m platformio run -e esp32s2dev` | Espressif 32 platform 54.3.20, Arduino core 3.2.0, IDF libs 5.4.0 | Local validation gate; `esp32-s2-saola-1`, 4 MB flash-compatible config, USB CDC on boot |
| ESP32-S3 Arduino compile | `python -m platformio run -e esp32s3dev` | Espressif 32 platform 54.3.20, Arduino core 3.2.0, IDF libs 5.4.0 | Local validation gate; `esp32-s3-devkitc-1`, 4 MB flash-compatible config, no PSRAM dependency |
| ESP-IDF example guard | `python tools/check_idf_example_contract.py` | Python 3 | Local static contract gate for `examples/esp_idf/basic` |
| ESP-IDF example build | `idf.py -C examples/esp_idf/basic set-target esp32s3 build` | ESP-IDF v5.4 line | Release/CI gate; not locally validated unless `idf.py` or CI logs are available |
| ESP-IDF example build | `idf.py -C examples/esp_idf/basic set-target esp32s2 build` | ESP-IDF v5.4 line | Release/CI gate; not locally validated unless `idf.py` or CI logs are available |

These are compile/test gates unless a report explicitly states hardware was connected
and exercised. The Prompt 06 report records that local pure ESP-IDF `idf.py`
builds were not run because `idf.py` was unavailable in this shell.

## Running Tests

```bash
# Build for ESP32-S3 (verifies compilation)
python -m platformio run -e esp32s3dev

# Build for ESP32-S2
python -m platformio run -e esp32s2dev

# Run native unit tests (requires host GCC)
python -m platformio test -e native

# Check repo-standardized CLI/helper and core framework contracts
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py

# Check the pure ESP-IDF example contract
python tools/check_idf_example_contract.py

# Check host-side I2C HIL runner/docs contract
python tools/check_hil_contract.py

# Plan a HIL run without opening serial or claiming hardware validation
python tools/run_i2c_hil.py --dry-run

# Build the pure ESP-IDF diagnostic example if idf.py is installed
idf.py -C examples/esp_idf/basic set-target esp32s3 build
idf.py -C examples/esp_idf/basic set-target esp32s2 build
```

## Documentation

- [CHANGELOG](CHANGELOG.md)
- [Release Checklist](docs/PCA9555_RELEASE_CHECKLIST.md)
- [Hardware Validation Matrix](docs/PCA9555_HARDWARE_VALIDATION_MATRIX.md)
- [I2C HIL Runbook](docs/I2C_HIL_RUNBOOK.md)
- [I2C HIL Target Template](docs/I2C_HIL_TARGET_TEMPLATE.md)
- [I2C HIL Self-Test Report](docs/I2C_HIL_SELFTEST_REPORT.md)
- [Hardening Prompt 01 Report](docs/PCA9555_HARDENING_PROMPT_01_CORE_PORTABILITY_REPORT.md)
- [Hardening Prompt 02 Report](docs/PCA9555_HARDENING_PROMPT_02_DIRTY_STATE_REPORT.md)
- [Hardening Prompt 03 Report](docs/PCA9555_HARDENING_PROMPT_03_GLITCH_SAFE_DIRECTION_REPORT.md)
- [Hardening Prompt 04 Report](docs/PCA9555_HARDENING_PROMPT_04_INTERRUPT_ERRATA_REPORT.md)
- [Hardening Prompt 05 Report](docs/PCA9555_HARDENING_PROMPT_05_FAULT_INJECTION_TEST_MATRIX_REPORT.md)
- [Hardening Prompt 06 Report](docs/PCA9555_HARDENING_PROMPT_06_ESP_IDF_REPORT.md)
- [Hardening Prompt 07 Report](docs/PCA9555_HARDENING_PROMPT_07_DOCS_RELEASE_GATES_REPORT.md)
- [Release Notes v1.1.0](docs/releases/v1.1.0.md)
- [Release Notes v1.0.0](docs/releases/v1.0.0.md)
- [PCA9555 Implementation Manual](PCA9555_io_expander_implementation_manual.md)
- [Register Reference](docs/register_reference.md)
- [Auto-Increment Feature](docs/application_notes/auto_increment_feature.md)
- [Contributing Guide](CONTRIBUTING.md)

## License

MIT License - see [LICENSE](LICENSE) for details.

# PCA9555 Driver Library

Production-grade PCA9555 16-bit I/O expander I2C driver for ESP32 (Arduino/PlatformIO and ESP-IDF).

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

The full bring-up CLI is shared between Arduino and ESP-IDF:

```bash
cd examples/espidf_basic
idf.py set-target esp32s3
idf.py build
```

The ESP-IDF example uses `driver/i2c_master.h` through
`examples/common/IdfArduinoCompat.h` so it exposes the same commands and serial
output as `examples/01_basic_bringup_cli`.

Validation status: command parity is structural through shared source. Native
tests and Arduino ESP32-S2/S3 example builds passed during this port pass; pure
ESP-IDF `idf.py` builds and hardware smoke tests are still pending until an IDF
toolchain and target devices are available.

## Quick Start

```cpp
#include <Wire.h>
#include "PCA9555/PCA9555.h"

PCA9555::Status myI2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                           uint32_t timeoutMs, void* user);
PCA9555::Status myI2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                               uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                               void* user);

PCA9555::PCA9555 device;

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9, 400000);
  
  PCA9555::Config cfg;
  cfg.i2cWrite = myI2cWrite;
  cfg.i2cWriteRead = myI2cWriteRead;
  cfg.i2cUser = &Wire;
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
and not part of the installed library package. If you do not inject `Config::nowMs`, the
driver falls back to `millis()` on Arduino/native-test builds and `esp_timer_get_time()`
on ESP-IDF builds.

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

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - Initialize driver, verify device, apply configuration
- `void tick(uint32_t nowMs)` - Process pending operations (currently no-op, reserved)
- `void end()` - Shutdown driver and set pins to input when online; if already `OFFLINE`, it clears local state without extra I2C

### Diagnostics

- `Status probe()` - Check device presence via raw I2C (no health tracking)
- `Status recover()` - Attempt recovery with health tracking + re-apply the current runtime config

### Input API

- `Status readInputs(PortData& data)` - Read both input ports after configured polarity inversion
- `Status readInput(Port port, uint8_t& value)` - Read one input port after configured polarity inversion
- `Status readPin(Pin pin, bool& state)` - Read one input-register bit after configured polarity inversion

### Output API

- `Status writeOutputs(const PortData& data)` - Write both output latch registers
- `Status writeOutput(Port port, uint8_t value)` - Write one output latch register
- `Status readOutput(Port port, uint8_t& value)` - Read single output latch register
- `Status writePin(Pin pin, bool high)` - Write a single output latch bit (uses cached value)
- `Status readOutputPin(Pin pin, bool& high)` - Read single output latch bit
- `Status readOutputs(PortData& data)` - Read back output latch register values

### Configuration API

- `Status setConfiguration(const PortData& data)` - Set pin directions (1=input, 0=output)
- `Status setPortConfiguration(Port port, uint8_t value)` - Set single port direction
- `Status getPortConfiguration(Port port, uint8_t& value)` - Read single port direction
- `Status getConfiguration(PortData& data)` - Read pin direction configuration
- `Status setPolarity(const PortData& data)` - Set polarity inversion
- `Status setPortPolarity(Port port, uint8_t value)` - Set single port polarity
- `Status getPortPolarity(Port port, uint8_t& value)` - Read single port polarity
- `Status getPolarity(PortData& data)` - Read polarity inversion
- `Status setPinPolarity(Pin pin, bool inverted)` - Set single pin polarity (uses cached value)
- `Status getPinPolarity(Pin pin, bool& inverted)` - Read single pin polarity
- `Status setPinDirection(Pin pin, bool input)` - Set single pin direction (uses cached value)
- `Status getPinDirection(Pin pin, bool& input)` - Read single pin direction

### Bit Manipulation API

All methods operate on a 16-bit mask (bit 0 = P00, bit 15 = P17), use cached shadow
registers, and write both ports in a single 2-byte I2C burst. No I2C occurs when the
mask causes no change. If the write fails, the cached shadow state remains unchanged so
later single-pin and recovery operations do not build on a failed update.

- `Status setOutputBits(uint16_t mask)` - Set output latch bits HIGH (OR mask into shadow)
- `Status clearOutputBits(uint16_t mask)` - Clear output latch bits LOW (AND ~mask into shadow)
- `Status toggleOutputBits(uint16_t mask)` - Toggle output latch bits (XOR mask into shadow)
- `Status togglePin(Pin pin)` - Toggle a single output latch bit (1-byte write, no read)
- `Status configureInputBits(uint16_t mask)` - Set masked pins to INPUT direction
- `Status configureOutputBits(uint16_t mask)` - Set masked pins to OUTPUT direction
- `Status setInvertBits(uint16_t mask)` - Enable polarity inversion for masked pins
- `Status clearInvertBits(uint16_t mask)` - Disable polarity inversion for masked pins

### Register Access

- `Status readRegister(uint8_t reg, uint8_t& value)` - Read any register (0-7)
- `Status writeRegister(uint8_t reg, uint8_t value)` - Write writable register (2-7)
- `Status readRegisters(uint8_t startReg, uint8_t* buf, size_t len)` - Read 1-2 registers in one auto-increment pair; an odd start wraps to the pair mate
- `Status writeRegisters(uint8_t startReg, const uint8_t* buf, size_t len)` - Write 1-2 registers in one auto-increment pair; an odd start wraps to the pair mate

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

Failed `begin()` calls clear stale runtime/cache state before returning. This
prevents a later diagnostic snapshot from reporting old output, polarity, or
configuration shadows after an unsuccessful initialization attempt.

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

## Behavioral Contracts

1. **Threading model**: Single-threaded by default. Not thread-safe.
2. **Timing model**: `tick()` bounded; all I2C operations are blocking.
3. **Resource ownership**: Bus and pins provided by application via `Config`.
4. **Memory behavior**: The library performs no dynamic allocation in `begin()` or steady state.
5. **Error handling**: All fallible APIs return `Status`. Silent failure is not possible.
6. **Health behavior**: transport `IN_PROGRESS` statuses are passed through without
   incrementing success or failure counters. `OFFLINE` is latched; normal public
   I2C operations return `BUSY` with `Driver is offline; call recover()` without
   touching the bus until `recover()` succeeds.

`begin()` verifies presence by reading both Configuration Port registers. By default it requires the
POR default `0xFF/0xFF` state and returns `CONFIG_REG_MISMATCH` if the expander is already
configured. If your MCU can reset without power-cycling the PCA9555, set
`Config::requireConfigPortDefaults = false` so `begin()` will accept the existing device state and
immediately apply the requested runtime configuration.

## Interrupt Errata

The PCA9555 has a known issue: after reading input ports, the internal register pointer
sits at address 0x00. If another device on the same I2C bus is subsequently read, the
PCA9555 may incorrectly interpret the address byte as a register write, potentially
clearing the interrupt output. Setting `Config::applyInterruptErrata = true` (default)
causes the driver to write a safe command byte (0x02) after every input read to move
the register pointer away from 0x00.

## Device Notes

- Reading the **Output Port** register returns the output latch (flip-flop), not the actual pin voltage.
- Reading the **Input Port** register reports the input-register sense after configured polarity inversion. With normal polarity this corresponds to the physical pin level, including when the pin is configured as an output.
- Each PCA9555 I/O has an internal ~100 kOhm pull-up when configured as an input. Inputs held low draw extra standby current, so unused inputs should be left high or configured as outputs driven high in low-power designs.

## Examples

### 01_basic_bringup_cli

Interactive serial CLI for device bringup and testing. It covers bus scan,
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

### espidf_basic

Pure ESP-IDF build of the same bring-up CLI. It includes the Arduino example
source with `PCA9555_EXAMPLE_PLATFORM_IDF=1`, supplies a small fixed-capacity
`String`/serial/GPIO/Wire-compatible shim, and backs I2C transactions with the
ESP-IDF v6 `i2c_master_*` APIs. The command set, output wording, health
diagnostics, self-test, stress commands, sweep, walk, and pattern flows stay
aligned with the Arduino CLI. `tools/check_cli_contract.py` checks the advertised
CLI command/help surface plus the IDF entry point and CMake dependency surface
so future wrapper edits cannot silently drop parity.

### Example Helpers (`examples/common/`)

| File | Purpose |
|------|---------|
| `BuildConfig.h` | Compile-time log level and core debug level |
| `Log.h` | Colorized serial logging macros (`LOGI`, `LOGE`, `LOGV`, etc.) |
| `BoardConfig.h` | Board-specific pin definitions and I2C init |
| `I2cTransport.h` | Wire-based transport adapter mapping to `Status` |
| `IdfArduinoCompat.h` | ESP-IDF-only example shim for the shared CLI |
| `TransportAdapter.h` | Alias wrapper matching the standardized helper layout |
| `I2cScanner.h` | I2C bus scanner utility |
| `CommandHandler.h` | Serial command-line helpers |
| `CliShell.h` | Serial line reader helper for CLI examples |
| `BusDiag.h` | Bus diagnostic scan wrapper |
| `HealthDiag.h` | Driver health snapshot printer |
| `HealthView.h` | Compact one-line health summary helper |

These helpers are **not** part of the library - they exist only to keep examples self-contained.

## Running Tests

```bash
# Build for ESP32-S3 (verifies compilation)
pio run -e esp32s3dev

# Build for ESP32-S2
pio run -e esp32s2dev

# Run native unit tests (requires host GCC)
pio test -e native

# Build the ESP-IDF full CLI example (requires ESP-IDF on PATH)
cd examples/espidf_basic
idf.py build

# Check repo-standardized CLI/helper and timing contracts
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
```

## Documentation

- [CHANGELOG](CHANGELOG.md)
- [Release Notes v1.1.0](docs/releases/v1.1.0.md)
- [Release Notes v1.0.0](docs/releases/v1.0.0.md)
- [PCA9555 Implementation Manual](PCA9555_io_expander_implementation_manual.md)
- [ESP-IDF Port Notes](docs/IDF_PORT.md)
- [ESP-IDF Port Implementation Notes](docs/IDF_PORT_IMPLEMENTATION.md)
- [Register Reference](docs/register_reference.md)
- [Auto-Increment Feature](docs/application_notes/auto_increment_feature.md)
- [Contributing Guide](CONTRIBUTING.md)
- `Doxyfile` indexes public headers, the ESP-IDF port notes, the shared Arduino
  CLI source, the native IDF entry point, and example-only framework shims.

## License

MIT License - see [LICENSE](LICENSE) for details.

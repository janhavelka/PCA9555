# PCA9555 Driver Library

Production-grade PCA9555 16-bit I/O expander I2C driver for ESP32 (Arduino/PlatformIO).

## Features

- **Injected I2C transport** - no Wire dependency in library code
- **Health monitoring** - automatic state tracking (READY/DEGRADED/OFFLINE)
- **Deterministic behavior** - no unbounded loops, no heap allocations
- **Managed synchronous lifecycle** - blocking I2C ops with clean begin/tick/end
- **Settings snapshot** - access the active runtime config and health counters with `getSettings()`
- **16-bit I/O** - two independent 8-bit ports (Port 0 and Port 1)
- **Interrupt errata workaround** - configurable automatic errata mitigation
- **Single-pin helpers** - pin-level readback plus atomic read-modify-write for output, direction, and polarity
- **Bulk register helpers** - pair-bounded `readRegisters()` / `writeRegisters()` for low-level diagnostics
- **Recoverable runtime state** - `recover()` reapplies the latest live output/config/polarity state

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
#include "common/I2cTransport.h"

PCA9555::PCA9555 device;

void setup() {
  Serial.begin(115200);
  transport::initWire(8, 9, 400000, 50);
  
  PCA9555::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  cfg.i2cAddress = 0x20;
  cfg.configPort0 = 0x0F;   // Port 0 lower nibble = input, upper = output
  cfg.outputPort0 = 0xF0;   // Set output pins high initially
  
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
  
  // Write a single pin
  device.writePin(12, true);  // Set pin 12 (Port 1, bit 4) high
  
  delay(1000);
}
```

The example adapter maps Arduino `Wire` failures to specific `I2C_*` status codes and keeps
bus timeout ownership in `transport::initWire()`. If you do not inject `Config::nowMs`, the
driver falls back to `millis()` on Arduino/native-test builds.

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

## API Reference

### Lifecycle

- `Status begin(const Config& config)` - Initialize driver, verify device, apply configuration
- `void tick(uint32_t nowMs)` - Process pending operations (currently no-op, reserved)
- `void end()` - Shutdown driver, set all pins to input (safe state)

### Diagnostics

- `Status probe()` - Check device presence via raw I2C (no health tracking)
- `Status recover()` - Attempt recovery with health tracking + re-apply the current runtime config

### Input API

- `Status readInputs(PortData& data)` - Read both input ports
- `Status readInput(Port port, uint8_t& value)` - Read single input port
- `Status readPin(Pin pin, bool& state)` - Read single input pin (0-15)

### Output API

- `Status writeOutputs(const PortData& data)` - Write both output ports
- `Status writeOutput(Port port, uint8_t value)` - Write single output port
- `Status readOutput(Port port, uint8_t& value)` - Read single output latch register
- `Status writePin(Pin pin, bool high)` - Write single output pin (uses cached value)
- `Status readOutputPin(Pin pin, bool& high)` - Read single output latch bit
- `Status readOutputs(PortData& data)` - Read back output register values

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

### Register Access

- `Status readRegister(uint8_t reg, uint8_t& value)` - Read any register (0-7)
- `Status writeRegister(uint8_t reg, uint8_t value)` - Write writable register (2-7)
- `Status readRegisters(uint8_t startReg, uint8_t* buf, size_t len)` - Read 1-2 registers in one pair
- `Status writeRegisters(uint8_t startReg, const uint8_t* buf, size_t len)` - Write 1-2 registers in one pair

### Health

- `DriverState state()` - Current driver state
- `bool isInitialized()` - True after `begin()` succeeds and before `end()`
- `bool isOnline()` - True if READY or DEGRADED
- `const Config& getConfig()` - Current recoverable runtime configuration snapshot
- `SettingsSnapshot getSettings()` - Combined settings plus health snapshot
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
| `outputPort0/1` | `0xFF` | Initial output values |
| `polarityPort0/1` | `0x00` | Polarity inversion (1=inverted) |
| `requireConfigPortDefaults` | `true` | Require Configuration Port 0/1 = `0xFF` at `begin()` |
| `applyInterruptErrata` | `true` | Enable interrupt errata workaround |

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

- Reading the **Output Port** register returns the output latch (flip-flop), not the actual pin voltage. Use `readInput()` / `readPin()` when you need the physical pin level.
- The **Input Port** register reflects the real pin level even when a pin is configured as an output, which is useful for output verification and fault checks.
- Each PCA9555 I/O has an internal ~100 kOhm pull-up when configured as an input. Inputs held low draw extra standby current, so unused inputs should be left high or configured as outputs driven high in low-power designs.

## Examples

### 01_basic_bringup_cli

Interactive serial CLI for device bringup and testing. Supports reading/writing all
ports and individual pins, register dump, direction and polarity configuration,
self-test, stress tests, driver health diagnostics, `cfg/settings` snapshots,
single-pin latch/direction/polarity readback (`rout`, `rdir`, `rpol`), `pininfo`,
full `pins` summaries, port-specific readback commands, and pair-bounded low-level
register access (`read regs` / `write regs`),
and both terse and descriptive command aliases (`read inputs`, `write pin`, `read reg`, ...).

Typical bring-up commands:

```text
scan
cfg
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

These helpers are **not** part of the library — they exist only to keep examples self-contained.

## Running Tests

```bash
# Build for ESP32-S3 (verifies compilation)
pio run -e esp32s3dev

# Build for ESP32-S2
pio run -e esp32s2dev

# Run native unit tests (requires host GCC)
pio test -e native

# Check repo-standardized CLI/helper and timing contracts
python tools/check_cli_contract.py
python tools/check_core_timing_guard.py
```

## Documentation

- [CHANGELOG](CHANGELOG.md)
- [PCA9555 Implementation Manual](PCA9555_io_expander_implementation_manual.md)
- [Register Reference](docs/register_reference.md)
- [Auto-Increment Feature](docs/application_notes/auto_increment_feature.md)
- [Contributing Guide](CONTRIBUTING.md)

## License

MIT License - see [LICENSE](LICENSE) for details.

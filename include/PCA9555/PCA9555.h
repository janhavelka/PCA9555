/// @file PCA9555.h
/// @brief Main driver class for PCA9555 16-bit I/O expander
#pragma once

#include <cstddef>
#include <cstdint>
#include "PCA9555/Status.h"
#include "PCA9555/Config.h"
#include "PCA9555/CommandTable.h"
#include "PCA9555/Version.h"

namespace PCA9555 {

/// Driver state for health monitoring
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// 16-bit port data (both ports combined)
struct PortData {
  uint8_t port0 = 0;  ///< Port 0 data (P00–P07)
  uint8_t port1 = 0;  ///< Port 1 data (P10–P17)

  /// Get combined 16-bit value (port1 << 8 | port0)
  /// @return Combined 16-bit value
  uint16_t combined() const {
    return static_cast<uint16_t>((static_cast<uint16_t>(port1) << 8) | port0);
  }

  /// Create from combined 16-bit value
  /// @param value Combined 16-bit value (low byte = port0, high byte = port1)
  /// @return PortData with port0 and port1 set
  static PortData fromCombined(uint16_t value) {
    PortData d;
    d.port0 = static_cast<uint8_t>(value & 0xFF);
    d.port1 = static_cast<uint8_t>((value >> 8) & 0xFF);
    return d;
  }
};

/// Snapshot of the current driver settings and health state.
struct SettingsSnapshot {
  Config config;                 ///< Active runtime configuration snapshot
  DriverState state = DriverState::UNINIT;
  bool initialized = false;
  uint32_t lastOkMs = 0;
  uint32_t lastErrorMs = 0;
  Status lastError = Status::Ok();
  uint8_t consecutiveFailures = 0;
  uint32_t totalFailures = 0;
  uint32_t totalSuccess = 0;
};

/// PCA9555 driver class
class PCA9555 {
public:
  // =========================================================================
  // Lifecycle
  // =========================================================================
  
  /// Initialize the driver with configuration.
  /// Sets output values before configuring directions to avoid glitches.
  /// Clears pending interrupts by reading input ports.
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() on success, error otherwise
  Status begin(const Config& config);
  
  /// Process pending operations (call regularly from loop).
  /// Currently a no-op; reserved for future periodic polling support.
  /// @param nowMs Current timestamp in milliseconds
  void tick(uint32_t nowMs);
  
  /// Shutdown the driver (sets all pins to input/high-Z).
  void end();
  
  // =========================================================================
  // Diagnostics
  // =========================================================================
  
  /// Check if device is present on the bus (no health tracking).
  /// Reads a configuration register via the raw transport path without enforcing
  /// the POR-default contents.
  /// @return Status::Ok() if device responds, error otherwise
  Status probe();
  
  /// Attempt to recover from DEGRADED/OFFLINE state.
  /// Re-probes device and re-applies configuration if successful.
  /// @return Status::Ok() if device now responsive, error otherwise
  Status recover();
  
  // =========================================================================
  // Driver State
  // =========================================================================
  
  /// Get current driver state
  /// @return Current DriverState
  DriverState state() const { return _driverState; }

  /// Check if begin() has completed successfully.
  /// @return true after begin() succeeds and before end() is called
  bool isInitialized() const { return _initialized; }
  
  /// Check if driver is ready for operations
  /// @return true if READY or DEGRADED
  bool isOnline() const { 
    return _driverState == DriverState::READY || 
           _driverState == DriverState::DEGRADED; 
  }

  /// Get a copy of the active configuration.
  /// Runtime mutators update this configuration so recover() re-applies the
  /// current desired state rather than the original power-on settings.
  const Config& getConfig() const { return _config; }

  /// Get a snapshot of the active settings and health counters.
  /// @return Copy of the current runtime settings and diagnostic state
  SettingsSnapshot getSettings() const;
  
  // =========================================================================
  // Health Tracking
  // =========================================================================
  
  /// Timestamp of last successful I2C operation
  /// @return Timestamp in milliseconds (0 if none)
  uint32_t lastOkMs() const { return _lastOkMs; }
  
  /// Timestamp of last failed I2C operation
  /// @return Timestamp in milliseconds (0 if none)
  uint32_t lastErrorMs() const { return _lastErrorMs; }
  
  /// Most recent error status
  /// @return Most recent error Status
  Status lastError() const { return _lastError; }
  
  /// Consecutive failures since last success
  /// @return Count of consecutive failures
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  
  /// Total failure count (lifetime)
  /// @return Lifetime failure count (wraps at max)
  uint32_t totalFailures() const { return _totalFailures; }
  
  /// Total success count (lifetime)
  /// @return Lifetime success count (wraps at max)
  uint32_t totalSuccess() const { return _totalSuccess; }
  
  // =========================================================================
  // Input API
  // =========================================================================
  
  /// Read both input ports in a single burst transaction.
  /// Applies interrupt errata workaround if configured.
  /// @param[out] data Port 0 and Port 1 input values
  /// @return Status::Ok() on success
  Status readInputs(PortData& data);

  /// Read a single input port.
  /// Applies interrupt errata workaround if configured.
  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value 8-bit port value
  /// @return Status::Ok() on success
  Status readInput(Port port, uint8_t& value);

  /// Read a single pin state (0 or 1).
  /// @param pin Pin number 0–15 (0–7 = Port 0, 8–15 = Port 1)
  /// @param[out] state true if pin is high, false if low
  /// @return Status::Ok() on success
  Status readPin(Pin pin, bool& state);
  
  // =========================================================================
  // Output API
  // =========================================================================
  
  /// Write both output ports in a single burst transaction.
  /// @param data Port 0 and Port 1 output values
  /// @return Status::Ok() on success
  Status writeOutputs(const PortData& data);

  /// Write a single output port.
  /// @param port Port to write (PORT_0 or PORT_1)
  /// @param value 8-bit port value
  /// @return Status::Ok() on success
  Status writeOutput(Port port, uint8_t value);

  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value 8-bit output latch value
  /// @return Status::Ok() on success
  /// Read back a single output port register value.
  /// Returns the latched output flip-flop state, not the sampled pin level.
  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value 8-bit output latch value
  /// @return Status::Ok() on success
  Status readOutput(Port port, uint8_t& value);

  /// Set a single output pin high or low.
  /// Uses read-modify-write on the output register.
  /// @param pin Pin number 0–15
  /// @param high true = drive high, false = drive low
  /// @return Status::Ok() on success
  Status writePin(Pin pin, bool high);

  /// @param pin Pin number 0–15
  /// @param[out] high true if the output latch bit is set
  /// @return Status::Ok() on success
  /// Read back a single output pin latch state.
  /// Returns the stored output-register bit, not the sampled pin level.
  /// @param pin Pin number 0-15
  /// @param[out] high true if the output latch bit is 1
  /// @return Status::Ok() on success
  Status readOutputPin(Pin pin, bool& high);

  /// Read back the output port register values (flip-flop, not actual pin state).
  /// @param[out] data Port 0 and Port 1 output register values
  /// @return Status::Ok() on success
  Status readOutputs(PortData& data);
  
  // =========================================================================
  // Configuration API
  // =========================================================================

  /// Set pin direction for both ports.
  /// Bit = 1: input, Bit = 0: output.
  /// @param data Port 0 and Port 1 configuration values
  /// @return Status::Ok() on success
  Status setConfiguration(const PortData& data);

  /// Set pin direction for a single port.
  /// @param port Port to configure
  /// @param value Direction bits (1=input, 0=output)
  /// @return Status::Ok() on success
  Status setPortConfiguration(Port port, uint8_t value);

  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value Direction bits (1=input, 0=output)
  /// @return Status::Ok() on success
  /// Read back a single port configuration register.
  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value Direction bits (1=input, 0=output)
  /// @return Status::Ok() on success
  Status getPortConfiguration(Port port, uint8_t& value);

  /// Read current pin direction configuration.
  /// @param[out] data Port 0 and Port 1 configuration values  
  /// @return Status::Ok() on success
  Status getConfiguration(PortData& data);

  /// Set polarity inversion for both ports.
  /// Bit = 1: invert input, Bit = 0: no inversion.
  /// @param data Port 0 and Port 1 polarity inversion values
  /// @return Status::Ok() on success
  Status setPolarity(const PortData& data);

  /// Set polarity inversion for a single port.
  /// @param port Port to configure
  /// @param value Polarity inversion bits
  /// @return Status::Ok() on success
  Status setPortPolarity(Port port, uint8_t value);

  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value Polarity inversion bits
  /// @return Status::Ok() on success
  /// Read back a single port polarity inversion register.
  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value Polarity inversion bits
  /// @return Status::Ok() on success
  Status getPortPolarity(Port port, uint8_t& value);

  /// Read current polarity inversion settings.
  /// @param[out] data Port 0 and Port 1 polarity values
  /// @return Status::Ok() on success
  Status getPolarity(PortData& data);

  /// Configure polarity inversion for a single pin.
  /// Uses read-modify-write on the cached polarity register state.
  /// @param pin Pin number 0-15
  /// @param inverted true = invert input sense, false = normal polarity
  /// @return Status::Ok() on success
  Status setPinPolarity(Pin pin, bool inverted);

  /// @param pin Pin number 0-15
  /// @param[out] inverted true if input polarity is inverted
  /// @return Status::Ok() on success
  /// Read back the configured polarity inversion for a single pin.
  /// @param pin Pin number 0-15
  /// @param[out] inverted true if the pin input is inverted
  /// @return Status::Ok() on success
  Status getPinPolarity(Pin pin, bool& inverted);

  /// Configure a single pin as input or output.
  /// Uses read-modify-write on the configuration register.
  /// @param pin Pin number 0–15
  /// @param input true = configure as input, false = output
  /// @return Status::Ok() on success
  Status setPinDirection(Pin pin, bool input);

  /// @param pin Pin number 0–15
  /// @param[out] input true if configured as input, false if configured as output
  /// @return Status::Ok() on success
  /// Read back the configured direction for a single pin.
  /// @param pin Pin number 0-15
  /// @param[out] input true if the pin is configured as input
  /// @return Status::Ok() on success
  Status getPinDirection(Pin pin, bool& input);

  // =========================================================================
  // Register Access (Direct)
  // =========================================================================

  /// Read a single register by command byte.
  /// @param reg Register address (0x00–0x07)
  /// @param[out] value Register value
  /// @return Status::Ok() on success
  Status readRegister(uint8_t reg, uint8_t& value);

  /// Read multiple consecutive registers within a single register pair.
  /// Bulk reads are limited to 1-2 bytes and must not cross a pair boundary.
  /// The cached runtime state is synchronized for any writable registers read.
  /// @param startReg Starting register address (0x00-0x07)
  /// @param[out] buf Destination buffer
  /// @param len Number of bytes to read
  /// @return Status::Ok() on success
  Status readRegisters(uint8_t startReg, uint8_t* buf, size_t len);

  /// Write a single register by command byte.
  /// @param reg Register address (0x02–0x07, input regs are read-only)
  /// @param value Value to write
  /// @return Status::Ok() on success
  Status writeRegister(uint8_t reg, uint8_t value);

  /// Write multiple consecutive registers within a single register pair.
  /// Bulk writes are limited to 1-2 bytes and must not cross a pair boundary.
  /// The cached runtime state is synchronized after a successful write.
  /// @param startReg Starting register address (0x02-0x07)
  /// @param buf Source buffer
  /// @param len Number of bytes to write
  /// @return Status::Ok() on success
  Status writeRegisters(uint8_t startReg, const uint8_t* buf, size_t len);

private:
  // =========================================================================
  // Transport Wrappers
  // =========================================================================
  
  /// Raw I2C write-read (no health tracking)
  Status _i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen, 
                          uint8_t* rxBuf, size_t rxLen);
  
  /// Raw I2C write (no health tracking)
  Status _i2cWriteRaw(const uint8_t* buf, size_t len);
  
  /// Tracked I2C write-read (updates health)
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen, 
                              uint8_t* rxBuf, size_t rxLen);
  
  /// Tracked I2C write (updates health)
  Status _i2cWriteTracked(const uint8_t* buf, size_t len);
  
  // =========================================================================
  // Register Access (Internal)
  // =========================================================================
  
  /// Read registers (uses tracked path)
  Status readRegs(uint8_t startReg, uint8_t* buf, size_t len);
  
  /// Write registers (uses tracked path)
  Status writeRegs(uint8_t startReg, const uint8_t* buf, size_t len);

  /// Read single register (raw path, no health tracking)
  Status _readRegisterRaw(uint8_t reg, uint8_t& value);
  
  // =========================================================================
  // Health Management
  // =========================================================================
  
  /// Update health counters and state based on operation result.
  /// Called ONLY from tracked transport wrappers.
  Status _updateHealth(const Status& st);

  // =========================================================================
  // Internal Helpers
  // =========================================================================

  /// Apply configuration from Config to device registers.
  /// Order: output values → polarity → direction → read inputs (clear INT).
  Status _applyConfig();

  /// Apply interrupt errata workaround: write a safe command byte after input reads.
  Status _applyInterruptErrata();

  /// Synchronize cached runtime state after direct register access.
  void _syncShadowRegister(uint8_t reg, uint8_t value);

  /// Get current timestamp in milliseconds
  uint32_t _nowMs() const;
  
  // =========================================================================
  // State
  // =========================================================================
  
  Config _config;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;
  
  // Health counters
  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;

  // Cached output register state (for read-modify-write on single pins)
  uint8_t _cachedOutput0 = 0xFF;
  uint8_t _cachedOutput1 = 0xFF;
  uint8_t _cachedConfig0 = 0xFF;
  uint8_t _cachedConfig1 = 0xFF;
};

} // namespace PCA9555

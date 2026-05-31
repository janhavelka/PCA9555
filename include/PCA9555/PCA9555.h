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

/// @brief Driver state for health monitoring.
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// @brief 16-bit port data for both ports.
struct PortData {
  uint8_t port0 = 0;  ///< Port 0 data (P00-P07)
  uint8_t port1 = 0;  ///< Port 1 data (P10-P17)

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

/// @brief Snapshot of the current driver settings and health state.
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
  bool hardwareStateDirty = false;       ///< Hardware/cache divergence is possible
  Status hardwareStateDirtyError = Status::Ok(); ///< Original write error that marked dirty
};

/// @brief Framework-neutral PCA9555 driver class.
///
/// The driver uses only injected transport callbacks for I2C access and does
/// not own or initialize the bus. Optional Config::i2cLock/i2cUnlock callbacks
/// can serialize compound input-read plus errata-write sequences; otherwise
/// shared-bus serialization remains the application's responsibility.
/// Instances are single threaded and non-reentrant. Public APIs that touch I2C
/// may block in the injected transport and are not ISR-safe. Transport callbacks
/// must not recursively call methods on the same PCA9555 instance.
///
/// PCA9555 input, output, and configuration registers are independent. The
/// Input Port registers report sampled pin sense after polarity inversion. The
/// Output Port registers hold latch bits; pins follow those latches only while
/// their Configuration bits are 0 (output). Safe input-to-output changes write
/// the intended latch value before clearing a Configuration bit.
///
/// Instances are intentionally non-copyable and non-movable. Keep each driver
/// object in stable storage for its lifetime and pass it by reference or pointer.
class PCA9555 {
public:
  PCA9555() = default;
  /// Copying is disabled because driver instances own cached hardware state.
  PCA9555(const PCA9555&) = delete;
  /// Copy assignment is disabled because driver instances own cached hardware state.
  PCA9555& operator=(const PCA9555&) = delete;
  /// Moving is disabled because transport callbacks and cached state must stay stable.
  PCA9555(PCA9555&&) = delete;
  /// Move assignment is disabled because transport callbacks and cached state must stay stable.
  PCA9555& operator=(PCA9555&&) = delete;

  // =========================================================================
  // Lifecycle
  // =========================================================================
  
  /// Initialize the driver with configuration.
  /// Sets output latch values before configuring directions to avoid glitches.
  /// Startup order is output latch -> polarity -> direction -> input read ->
  /// optional interrupt errata workaround.
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() on success, error otherwise
  Status begin(const Config& config);
  
  /// Process pending operations from application context.
  /// Currently a no-op; reserved for future periodic polling support.
  /// @param nowMs Current monotonic timestamp in milliseconds. Use the same
  /// clock domain as Config::nowMs when that callback is supplied.
  void tick(uint32_t nowMs);
  
  /// Shutdown the driver.
  /// If initialized and not OFFLINE, attempts a best-effort raw write to set
  /// all pins to input/high-Z. If already OFFLINE, clears local state without
  /// touching the bus.
  void end();
  
  // =========================================================================
  // Diagnostics
  // =========================================================================
  
  /// Check if the configured address responds on the bus (no health tracking).
  /// Reads a configuration register via the raw transport path without enforcing
  /// the POR-default contents. PCA9555 has no chip-ID register; this is address
  /// response only, not identity proof.
  /// @return Status::Ok() if the address responds, error otherwise
  Status probe();
  
  /// Attempt to recover from DEGRADED/OFFLINE state.
  /// Performs a tracked configuration-register read and re-applies the current
  /// runtime configuration if successful. A full successful recover() clears
  /// hardwareStateDirty(); failed or partial recovery leaves it set. This cannot
  /// force a true PCA9555 power-on reset.
  /// recover() is the only public I2C path allowed to probe the bus while the
  /// driver is OFFLINE. If recovery starts from OFFLINE and fails, OFFLINE is
  /// re-latched and normal I2C APIs remain blocked.
  /// @return Status::Ok() if the address responds and reapply succeeds, error otherwise
  Status recover();
  
  // =========================================================================
  // Driver State
  // =========================================================================
  
  /// Get current driver state
  /// @return Current DriverState
  DriverState state() const { return _driverState; }

  /// Alias for state() for cross-driver diagnostics.
  /// @return Current DriverState
  DriverState driverState() const { return state(); }

  /// Check if begin() has completed successfully.
  /// @return true after begin() succeeds and before end() is called
  bool isInitialized() const { return _initialized; }
  
  /// Check if driver is ready for operations
  /// @return true if READY or DEGRADED
  bool isOnline() const { 
    return _driverState == DriverState::READY || 
           _driverState == DriverState::DEGRADED; 
  }

  /// Get the active configuration.
  /// Runtime mutators update this configuration so recover() re-applies the
  /// current desired state rather than the original power-on settings.
  const Config& getConfig() const { return _config; }

  /// Get a snapshot of the active settings and health counters.
  /// @return Copy of the current runtime settings and diagnostic state
  SettingsSnapshot getSettings() const;

  /// Copy the active settings and health counters.
  /// @param[out] out Receives the current runtime settings and diagnostic state
  /// @return Status::Ok()
  Status getSettings(SettingsSnapshot& out) const;
  
  // =========================================================================
  // Health Tracking
  // =========================================================================
  
  /// Timestamp of last successful I2C operation.
  /// @return Timestamp in milliseconds, or 0 if none or Config::nowMs is absent
  uint32_t lastOkMs() const { return _lastOkMs; }
  
  /// Timestamp of last failed I2C operation.
  /// @return Timestamp in milliseconds, or 0 if none or Config::nowMs is absent
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

  /// Check whether hardware may no longer match the cached desired state.
  ///
  /// I2C writes are not treated as atomic: a transport may return failure after
  /// one or more register bytes reached the expander. Validation failures,
  /// NOT_INITIALIZED, BUSY, reads, and IN_PROGRESS do not mark dirty.
  /// This is set when a register write returns failure after it may have
  /// reached the device. It clears after a full successful recover(), or after
  /// a successful begin() verifies the device and applies the requested state.
  /// @return true if hardware/cache state may be divergent
  bool hardwareStateDirty() const { return _hardwareStateDirty; }

  /// Get the transport error that marked hardware state dirty.
  /// The failed API still returns this same original transport Status.
  /// @return Original transport Status for the dirty condition, or Status::Ok()
  Status hardwareStateDirtyError() const { return _hardwareStateDirtyError; }
  
  // =========================================================================
  // Input API
  // =========================================================================
  
  /// Read both input ports in a single burst transaction.
  /// Returned bits are the PCA9555 input-register sense, including any
  /// configured polarity inversion.
  /// Clears both input-port interrupt sources. Applies interrupt errata
  /// workaround if configured; if the input read succeeds but the errata write
  /// fails, data is valid and the errata write Status is returned.
  /// @param[out] data Port 0 and Port 1 input values
  /// @return Status::Ok() on success
  Status readInputs(PortData& data);

  /// Read both input ports and return the 16-bit value for INT service.
  /// This is the preferred task/main-context API after an INT pin event. It
  /// clears both PCA9555 input-port interrupt sources by reading Input Port 0/1.
  /// If the input read succeeds but the errata write fails, value is valid and
  /// the errata write Status is returned.
  /// @param[out] value Combined value (bit 0 = P00, bit 15 = P17)
  /// @return Status::Ok() on success
  Status readInputsAndClearInterrupt(uint16_t& value);

  /// Clear PCA9555 input-port interrupt sources by reading both input ports.
  /// Does not write to the INT pin. Applies interrupt errata workaround if
  /// configured and returns that error if pointer parking fails.
  /// @return Status::Ok() on success
  Status clearInterrupts();

  /// Apply the TI interrupt errata workaround directly.
  /// Writes cmd::ERRATA_SAFE_CMD to park the command pointer at a nonzero
  /// register address. Normally this is called automatically after input reads.
  /// This direct helper does not acquire Config::i2cLock; callers must provide
  /// any required shared-bus serialization.
  /// @return Status::Ok() on success
  Status applyInterruptErrataWorkaround();

  /// Read a single input port.
  /// Returned bits are the PCA9555 input-register sense, including any
  /// configured polarity inversion.
  /// Clears only the selected port's interrupt source. Applies interrupt errata
  /// workaround if configured; if the input read succeeds but the errata write
  /// fails, value is valid and the errata write Status is returned.
  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value 8-bit port value
  /// @return Status::Ok() on success
  Status readInput(Port port, uint8_t& value);

  /// Read a single input-register bit (0 or 1).
  /// The reported sense includes any configured polarity inversion.
  /// Clears only the containing port's interrupt source.
  /// If the input read succeeds but the errata write fails, state is valid and
  /// the errata write Status is returned.
  /// @param pin Pin number 0-15 (0-7 = Port 0, 8-15 = Port 1)
  /// @param[out] state true if the input-register bit is 1, false if 0
  /// @return Status::Ok() on success
  Status readPin(Pin pin, bool& state);
  
  // =========================================================================
  // Output API
  // =========================================================================
  
  /// Write both output latch registers in a single burst transaction.
  /// Physical pins follow these latches only when configured as outputs.
  /// @param data Port 0 and Port 1 output latch values
  /// @return Status::Ok() on success
  Status writeOutputs(const PortData& data);

  /// Write a single output latch register.
  /// Physical pins follow this latch only when configured as outputs.
  /// @param port Port to write (PORT_0 or PORT_1)
  /// @param value 8-bit output latch value
  /// @return Status::Ok() on success
  Status writeOutput(Port port, uint8_t value);

  /// Read back a single output port register value.
  /// Returns the latched output flip-flop state, not the sampled pin level.
  /// @param port Port to read (PORT_0 or PORT_1)
  /// @param[out] value 8-bit output latch value
  /// @return Status::Ok() on success
  Status readOutput(Port port, uint8_t& value);

  /// Set a single output latch bit high or low.
  /// Uses read-modify-write on the cached output register state.
  /// The physical pin follows this latch only when configured as an output.
  /// @param pin Pin number 0-15
  /// @param high true = latch high, false = latch low
  /// @return Status::Ok() on success
  Status writePin(Pin pin, bool high);

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

  /// Force-preload one output latch bit without changing pin direction.
  /// This writes the output register even if the cached latch already matches.
  /// Use before enabling an output when the latch may have been changed by
  /// reset, external diagnostics, or a previous dirty-state condition.
  /// @param pin Pin number 0-15
  /// @param high true = preload latch high, false = preload latch low
  /// @return Status::Ok() on success
  Status preloadOutput(Pin pin, bool high);

  /// Force-preload selected output latch bits without changing directions.
  /// Bits outside mask keep their cached desired latch value. At least one
  /// output-register write is performed whenever mask is nonzero.
  /// Use configureOutputs() when the same operation should also enable outputs.
  /// @param mask 16-bit mask selecting latch bits to preload
  /// @param values 16-bit values; selected 1 bits preload HIGH, 0 bits LOW
  /// @return Status::Ok() on success
  Status preloadOutputs(uint16_t mask, uint16_t values);

  // =========================================================================
  // Bit Manipulation API
  // =========================================================================

  /// Set specific output latch bits HIGH without affecting other bits.
  /// Physical pins follow these latch bits only when configured as outputs.
  /// Applies mask via OR to cached output shadow registers and writes both
  /// ports in a single 2-byte burst. No I2C occurs if the mask causes no
  /// change (all targeted bits already HIGH).
  /// @param mask 16-bit mask (bit 0 = P00 … bit 15 = P17); 1 = set HIGH
  /// @return Status::Ok() on success
  Status setOutputBits(uint16_t mask);

  /// Clear specific output latch bits to LOW without affecting other bits.
  /// Physical pins follow these latch bits only when configured as outputs.
  /// Applies inverted mask via AND to cached output shadow registers and
  /// writes both ports in a single 2-byte burst. No I2C occurs if no change.
  /// @param mask 16-bit mask; 1 = clear to LOW
  /// @return Status::Ok() on success
  Status clearOutputBits(uint16_t mask);

  /// Toggle specific output latch bits without affecting other bits.
  /// Physical pins follow these latch bits only when configured as outputs.
  /// Applies mask via XOR to cached output shadow registers and writes both
  /// ports in a single 2-byte burst. No I2C occurs if mask is zero.
  /// @param mask 16-bit mask; 1 = toggle
  /// @return Status::Ok() on success
  Status toggleOutputBits(uint16_t mask);

  /// Toggle a single output latch bit using the cached shadow register.
  /// The physical pin follows this latch only when configured as an output.
  /// Performs a single 1-byte I2C write without a preceding read.
  /// @param pin Pin number 0-15
  /// @return Status::Ok() on success
  Status togglePin(Pin pin);

  /// Configure masked pins as inputs (set configuration register bits to 1).
  /// Applies mask via OR to cached configuration shadow registers and writes
  /// both ports in a single 2-byte burst. No I2C occurs if no change.
  /// Output-to-input transitions may trigger PCA9555 interrupt behavior if the
  /// sampled input state differs from the previous input-register state.
  /// @param mask 16-bit mask; 1 = set direction to INPUT
  /// @return Status::Ok() on success
  Status configureInputBits(uint16_t mask);

  /// Configure masked pins as outputs (clear configuration register bits to 0).
  /// Legacy helper that uses the cached output latch values as the desired
  /// preload for any input-to-output transitions. Prefer configureOutputs()
  /// when the initial output level should be specified with the direction change.
  /// If the preload succeeds but the configuration write fails, the original
  /// transport error is returned and hardwareStateDirty() is set.
  /// @param mask 16-bit mask; 1 = set direction to OUTPUT
  /// @return Status::Ok() on success
  Status configureOutputBits(uint16_t mask);

  /// Enable polarity inversion for masked pins.
  /// Applies mask via OR to polarity registers and writes both ports in a
  /// single 2-byte burst. No I2C occurs if no change.
  /// @param mask 16-bit mask; 1 = enable inversion
  /// @return Status::Ok() on success
  Status setInvertBits(uint16_t mask);

  /// Disable polarity inversion for masked pins.
  /// Applies inverted mask via AND to polarity registers and writes both
  /// ports in a single 2-byte burst. No I2C occurs if no change.
  /// @param mask 16-bit mask; 1 = disable inversion
  /// @return Status::Ok() on success
  Status clearInvertBits(uint16_t mask);

  // =========================================================================
  // Configuration API
  // =========================================================================

  /// Set pin direction for both ports.
  /// Bit = 1: input, Bit = 0: output.
  /// Any input-to-output bits are force-preloaded with cached output latch
  /// values before the configuration write. Output-to-input bits write only the
  /// configuration register and may trigger PCA9555 interrupt behavior if the
  /// sampled input state changes. Prefer configureOutputs() for bulk output
  /// enabling with explicit latch values.
  /// If preload succeeds but the configuration write fails, the original error
  /// is returned and hardwareStateDirty() is set.
  /// @param data Port 0 and Port 1 configuration values
  /// @return Status::Ok() on success
  Status setConfiguration(const PortData& data);

  /// Set pin direction for a single port.
  /// Input-to-output bits are force-preloaded with cached output latch values
  /// before the configuration bit is cleared. Output-to-input bits write only
  /// the configuration register and may trigger PCA9555 interrupt behavior.
  /// If preload succeeds but the configuration write fails, the original error
  /// is returned and hardwareStateDirty() is set.
  /// @param port Port to configure
  /// @param value Direction bits (1=input, 0=output)
  /// @return Status::Ok() on success
  Status setPortConfiguration(Port port, uint8_t value);

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

  /// Read back the configured polarity inversion for a single pin.
  /// @param pin Pin number 0-15
  /// @param[out] inverted true if the pin input is inverted
  /// @return Status::Ok() on success
  Status getPinPolarity(Pin pin, bool& inverted);

  /// Configure a single pin as input or output.
  /// Legacy helper using bool direction. When changing an input to output, the
  /// cached output latch bit is force-preloaded before direction is changed.
  /// Prefer preloadOutput() plus setDirection(), or configureOutputs(), when the
  /// initial output level should be explicit at the call site.
  /// Changing output to input may trigger PCA9555 interrupt behavior.
  /// If preload succeeds but the configuration write fails, the original error
  /// is returned and hardwareStateDirty() is set.
  /// @param pin Pin number 0-15
  /// @param input true = configure as input, false = output
  /// @return Status::Ok() on success
  Status setPinDirection(Pin pin, bool input);

  /// Configure a single pin direction using explicit Direction values.
  /// Changing an input to output force-preloads the cached output latch bit
  /// before clearing the configuration bit. Changing output to input writes only
  /// the configuration register and may trigger PCA9555 interrupt behavior if
  /// the input sense changes.
  /// Use preloadOutput() first when the desired output level differs from the
  /// cached latch value.
  /// If preload succeeds but the configuration write fails, the original error
  /// is returned and hardwareStateDirty() is set.
  /// @param pin Pin number 0-15
  /// @param direction Direction::INPUT_MODE or Direction::OUTPUT_MODE
  /// @return Status::Ok() on success
  Status setDirection(Pin pin, Direction direction);

  /// Safely configure masked pins as outputs with explicit initial latch values.
  /// Preferred API for enabling multiple outputs at runtime.
  /// The output latch write is performed first and must succeed before the
  /// configuration bits are cleared. If the direction write fails after preload,
  /// the original write error is returned and hardwareStateDirty() is set by the
  /// normal write-failure policy; the latch may be preloaded even though the
  /// direction may not have changed.
  /// @param outputMask 16-bit mask selecting pins to configure as outputs
  /// @param outputValues Initial output latch values for selected pins
  /// @return Status::Ok() on success
  Status configureOutputs(uint16_t outputMask, uint16_t outputValues);

  /// Read back the configured direction for a single pin.
  /// @param pin Pin number 0-15
  /// @param[out] input true if the pin is configured as input
  /// @return Status::Ok() on success
  Status getPinDirection(Pin pin, bool& input);

  // =========================================================================
  // Register Access (Direct)
  // =========================================================================

  /// Read a single register by command byte.
  /// @param reg Register address (0x00-0x07)
  /// @param[out] value Register value
  /// @return Status::Ok() on success
  Status readRegister(uint8_t reg, uint8_t& value);

  /// Read one or two registers within the selected register pair.
  /// If startReg is the odd register in a pair, the second byte wraps to the
  /// even register in that same pair, matching PCA9555 auto-increment behavior.
  /// The cached runtime state is synchronized for any writable registers read.
  /// Reading Input Port registers clears the corresponding port interrupt
  /// source and applies the errata workaround when configured. If an input read
  /// succeeds but the errata write fails, buf contains valid input data and the
  /// errata write Status is returned.
  /// @param startReg Starting register address (0x00-0x07)
  /// @param[out] buf Destination buffer
  /// @param len Number of bytes to read
  /// @return Status::Ok() on success
  Status readRegisters(uint8_t startReg, uint8_t* buf, size_t len);

  /// Write a single register by command byte.
  /// @param reg Register address (0x02-0x07, input regs are read-only)
  /// @param value Value to write
  /// @return Status::Ok() on success
  Status writeRegister(uint8_t reg, uint8_t value);

  /// Write one or two registers within the selected register pair.
  /// If startReg is the odd register in a pair, the second byte wraps to the
  /// even register in that same pair, matching PCA9555 auto-increment behavior.
  /// The cached runtime state is synchronized after a successful write.
  /// A failed direct write marks hardwareStateDirty() because hardware may have
  /// accepted one byte while the cache remained unchanged.
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

  /// Read input registers and optionally apply the errata workaround while the
  /// caller holds any configured bus lock.
  Status _readInputRegistersLocked(uint8_t startReg, uint8_t* buf, size_t len,
                                   bool& readCompleted);

  /// Read input registers and optionally apply the errata workaround under the
  /// configured optional bus lock.
  Status _readInputRegistersCompound(uint8_t startReg, uint8_t* buf, size_t len,
                                     bool& readCompleted);

  /// Read single register (raw path, no health tracking)
  Status _readRegisterRaw(uint8_t reg, uint8_t& value);

  /// Write configuration cache/registers after any required safe preload.
  Status _writeConfigurationNoPreload(const PortData& data);

  /// Write one configuration port after any required safe preload.
  Status _writePortConfigurationNoPreload(Port port, uint8_t value);
  
  // =========================================================================
  // Health Management
  // =========================================================================
  
  /// Update health counters and state based on operation result.
  /// Called ONLY from tracked transport wrappers.
  Status _updateHealth(const Status& st);
  void _reassertOfflineLatch();

  /// Mark hardware/cache state as possibly divergent after a failed write.
  void _markHardwareStateDirty(const Status& st);

  /// Clear hardware/cache divergence diagnostics after successful reconciliation.
  void _clearHardwareStateDirty();

  // =========================================================================
  // Internal Helpers
  // =========================================================================

  /// Apply configuration from Config to device registers.
  /// Order: output latch values -> polarity -> direction -> read inputs (clear INT).
  Status _applyConfig();

  /// Apply interrupt errata workaround without taking the optional bus lock.
  Status _applyInterruptErrataUnlocked();

  /// Acquire optional shared-bus lock.
  Status _lockBus(bool& locked);

  /// Release optional shared-bus lock when held.
  void _unlockBus(bool locked);

  /// Synchronize cached runtime state after direct register access.
  void _syncShadowRegister(uint8_t reg, uint8_t value);

  /// Get current diagnostic timestamp, or 0 when no clock hook is supplied.
  uint32_t _nowMs() const;
  
  // =========================================================================
  // State
  // =========================================================================
  
  Config _config;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;
  bool _allowOfflineI2c = false;
  
  // Health counters
  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;
  bool _hardwareStateDirty = false;
  Status _hardwareStateDirtyError = Status::Ok();

  // Cached output register state (for read-modify-write on single pins)
  uint8_t _cachedOutput0 = 0xFF;
  uint8_t _cachedOutput1 = 0xFF;
  uint8_t _cachedConfig0 = 0xFF;
  uint8_t _cachedConfig1 = 0xFF;
};

} // namespace PCA9555

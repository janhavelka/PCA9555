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
  bool outputDirty = false;    ///< Output latch state may differ from cached desired state
  bool polarityDirty = false;  ///< Polarity state may differ from cached desired state
  bool configDirty = false;    ///< Direction/config state may differ from cached desired state
};

/// PCA9555 driver class
class PCA9555 {
public:
  PCA9555() = default;
  PCA9555(const PCA9555&) = delete;
  PCA9555& operator=(const PCA9555&) = delete;
  PCA9555(PCA9555&&) = delete;
  PCA9555& operator=(PCA9555&&) = delete;

  // =========================================================================
  // Lifecycle
  // =========================================================================
  
  /// Initialize the driver with configuration.
  /// Sets output values before configuring directions to avoid glitches.
  /// Clears pending interrupts by reading input ports.
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() on success, error otherwise
  Status begin(const Config& config);
  
  /// Process one pending job instruction, if a chunked job is active.
  /// @param nowMs Current timestamp in milliseconds
  void tick(uint32_t nowMs);
  
  /// Shutdown the driver (sets all pins to input/high-Z).
  void end();

  // =========================================================================
  // Chunked Job API
  // =========================================================================

  /// Start a chunked input-port read job.
  /// The input register-pair read is one instruction. If
  /// Config::applyInterruptErrata is true, the pointer-park write is a second
  /// instruction. Read data becomes available through getLastReadInputs().
  /// @return Err::IN_PROGRESS when scheduled, Status::Ok() if already complete,
  ///         or an error when the job cannot be started
  Status startReadInputsJob();

  /// Start a chunked masked output update job.
  /// Applies value bits selected by mask to the cached output shadow and writes
  /// the output register pair as one instruction. No I2C occurs when mask causes
  /// no change.
  /// @param mask 16-bit mask (bit 0 = P00 ... bit 15 = P17)
  /// @param value Desired output bits; only bits selected by mask are used
  /// @return Err::IN_PROGRESS when scheduled, Status::Ok() for a CPU-only no-op,
  ///         or an error when the job cannot be started
  Status startWriteOutputsJob(uint16_t mask, uint16_t value);

  /// Start a chunked safe output-configuration job.
  /// Preloads the output latch for masked pins, then configures those pins as
  /// outputs. Each register-pair write is one instruction; no-op writes are
  /// skipped without consuming instruction budget.
  /// @param mask 16-bit mask selecting pins to configure as outputs
  /// @param value Desired output latch bits for the selected pins
  /// @return Err::IN_PROGRESS when scheduled, Status::Ok() for a CPU-only no-op,
  ///         or an error when the job cannot be started
  Status startConfigureOutputsJob(uint16_t mask, uint16_t value);

  /// Advance the active chunked job by at most maxInstructions I2C instructions.
  /// One register-pair read/write counts as one instruction. The interrupt
  /// errata pointer-park write also counts as one instruction.
  /// @param nowMs Current timestamp in milliseconds for health updates
  /// @param maxInstructions Maximum I2C instructions to execute this call
  /// @return Status::Ok() when no job remains, Err::IN_PROGRESS while active,
  ///         or the first transport error encountered
  Status pollJob(uint32_t nowMs, uint8_t maxInstructions);

  /// @return true while a chunked job is active
  bool jobActive() const;

  /// @return Status from the most recently completed or failed chunked job
  Status lastJobStatus() const;

  /// Copy the most recent chunked or blocking input read result.
  /// @param[out] data Last input data read by the driver
  /// @return Status::Ok()
  Status getLastReadInputs(PortData& data) const;
  
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

  /// Apply the interrupt errata workaround using the optional Config lock hooks.
  /// If Config::i2cLock/i2cUnlock are not configured, this is equivalent to
  /// applyInterruptErrataWorkaroundUnlocked().
  /// @return Status::Ok() on success
  Status applyInterruptErrataWorkaround();

  /// Apply the interrupt errata workaround without acquiring Config lock hooks.
  /// This writes only the safe command byte and is intended for callers that
  /// already own the bus lock.
  /// @return Status::Ok() on success
  Status applyInterruptErrataWorkaroundUnlocked();
  
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

  /// Check whether cached desired state may need hardware reapply.
  /// @return true if any recoverable register pair is dirty
  bool hasDirtyState() const { return _outputDirty || _polarityDirty || _configDirty; }
  
  // =========================================================================
  // Input API
  // =========================================================================
  
  /// Read both input ports in a single burst transaction.
  /// If Config::applyInterruptErrata is true, this is a compound synchronous
  /// helper: one input-register read followed by one safe command-byte write.
  /// @param[out] data Port 0 and Port 1 input values
  /// @return Status::Ok() on success
  Status readInputs(PortData& data);

  /// Read a single input port.
  /// If Config::applyInterruptErrata is true, this is a compound synchronous
  /// helper: one input-register read followed by one safe command-byte write.
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
  /// Uses the cached output shadow and writes the affected output port only
  /// when the logical value changes. If the output pair is dirty, a logical
  /// no-op replays the cached output pair.
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
  // Bit Manipulation API
  // =========================================================================

  /// Set specific output bits HIGH without affecting other bits.
  /// Applies mask via OR to cached output shadow registers and writes both
  /// ports in a single 2-byte burst. No I2C occurs if the mask causes no
  /// change (all targeted bits already HIGH).
  /// @param mask 16-bit mask (bit 0 = P00 … bit 15 = P17); 1 = set HIGH
  /// @return Status::Ok() on success
  Status setOutputBits(uint16_t mask);

  /// Clear specific output bits to LOW without affecting other bits.
  /// Applies inverted mask via AND to cached output shadow registers and
  /// writes both ports in a single 2-byte burst. No I2C occurs if no change.
  /// @param mask 16-bit mask; 1 = clear to LOW
  /// @return Status::Ok() on success
  Status clearOutputBits(uint16_t mask);

  /// Toggle specific output bits without affecting other bits.
  /// Applies mask via XOR to cached output shadow registers and writes both
  /// ports in a single 2-byte burst. No I2C occurs if mask is zero.
  /// @param mask 16-bit mask; 1 = toggle
  /// @return Status::Ok() on success
  Status toggleOutputBits(uint16_t mask);

  /// Toggle a single output pin using the cached shadow register.
  /// Performs a single 1-byte I2C write without a preceding read.
  /// @param pin Pin number 0–15
  /// @return Status::Ok() on success
  Status togglePin(Pin pin);

  /// Configure masked pins as inputs (set configuration register bits to 1).
  /// Applies mask via OR to cached configuration shadow registers and writes
  /// both ports in a single 2-byte burst. No I2C occurs if no change.
  /// @param mask 16-bit mask; 1 = set direction to INPUT
  /// @return Status::Ok() on success
  Status configureInputBits(uint16_t mask);

  /// Configure masked pins as outputs (clear configuration register bits to 0).
  /// Applies inverted mask via AND to cached configuration shadow registers
  /// and writes both ports in a single 2-byte burst. No I2C occurs if no change.
  /// @param mask 16-bit mask; 1 = set direction to OUTPUT
  /// @return Status::Ok() on success
  Status configureOutputBits(uint16_t mask);

  /// Safely configure masked pins as outputs with a desired initial latch value.
  /// Bits set in mask are configured as outputs. Matching bits in value select
  /// the output latch value: 1 = HIGH, 0 = LOW. Bits outside mask are ignored.
  /// This blocking compound helper preloads the output latch before changing
  /// direction and uses the same two-instruction sequence as
  /// startConfigureOutputsJob() with enough budget to complete.
  /// @param mask 16-bit mask selecting pins to configure as outputs
  /// @param value Desired output latch bits for the selected pins
  /// @return Status::Ok() on success
  Status configureOutputs(uint16_t mask, uint16_t value);

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
  /// Uses the cached polarity register state and writes the affected polarity
  /// port only when the logical value changes. If the polarity pair is dirty,
  /// a logical no-op replays the cached polarity pair.
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
  /// Uses the cached configuration shadow and writes the affected configuration
  /// port only when the logical value changes. If the configuration pair is
  /// dirty, a logical no-op replays the cached configuration pair.
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
  /// Input-register reads apply Config::applyInterruptErrata as a compound
  /// synchronous pointer-park write after the read.
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
  enum class JobType : uint8_t {
    NONE,
    READ_INPUTS,
    WRITE_OUTPUTS,
    CONFIGURE_OUTPUTS
  };

  enum class JobStep : uint8_t {
    NONE,
    READ_INPUT_PAIR,
    POINTER_PARK,
    WRITE_OUTPUT_PAIR,
    PRELOAD_OUTPUT_PAIR,
    WRITE_CONFIG_PAIR
  };

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

  /// Require begin() and not-OFFLINE for public I/O entry points.
  Status _requireReadyForPublicIo() const;

  /// Return OFFLINE when tracked public I/O is blocked.
  Status _offlineBlockedStatus() const;

  // =========================================================================
  // Internal Helpers
  // =========================================================================

  /// Apply configuration from Config to device registers.
  /// Order: output values → polarity → direction → read inputs (clear INT).
  Status _applyConfig();

  /// Apply interrupt errata workaround: write a safe command byte after input reads.
  Status _applyInterruptErrata();

  /// Acquire/release optional public API I2C lock hooks.
  Status _lockI2c();
  void _unlockI2c();

  /// Execute one active chunked-job I2C instruction.
  Status _executeJobInstruction();

  /// Clear active chunked-job state and store final status.
  void _finishJob(const Status& st);

  /// Synchronize cached runtime state after direct register access.
  void _syncShadowRegister(uint8_t reg, uint8_t value);

  /// Mark/clear dirty state for a recoverable register pair.
  void _markDirtyForRegister(uint8_t reg);
  void _clearDirtyForRegisterPair(uint8_t startReg, size_t len);

  /// Get current timestamp in milliseconds
  uint32_t _nowMs() const;
  
  // =========================================================================
  // State
  // =========================================================================
  
  Config _config;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;
  bool _allowOfflineIo = false;
  
  // Health counters
  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;

  // Chunked job state (single active job, no heap allocation)
  JobType _jobType = JobType::NONE;
  JobStep _jobStep = JobStep::NONE;
  Status _lastJobStatus = Status::Ok();
  PortData _lastInputData;
  uint8_t _jobOutput0 = 0xFF;
  uint8_t _jobOutput1 = 0xFF;
  uint8_t _jobConfig0 = 0xFF;
  uint8_t _jobConfig1 = 0xFF;
  bool _jobNeedsConfigWrite = false;
  bool _jobInstructionActive = false;
  uint32_t _pollNowMs = 0;
  bool _pollNowMsActive = false;

  // Cached register state for single-pin and mask helpers
  uint8_t _cachedOutput0 = 0xFF;
  uint8_t _cachedOutput1 = 0xFF;
  uint8_t _cachedConfig0 = 0xFF;
  uint8_t _cachedConfig1 = 0xFF;
  bool _outputDirty = false;
  bool _polarityDirty = false;
  bool _configDirty = false;
};

} // namespace PCA9555

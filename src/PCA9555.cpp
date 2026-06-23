/**
 * @file PCA9555.cpp
 * @brief PCA9555 16-bit I/O expander driver implementation.
 */

#include "PCA9555/PCA9555.h"

#include <cstring>
#include <limits>

namespace PCA9555 {
namespace {

static constexpr size_t MAX_BULK_LEN = 2;

static bool isValidAddress(uint8_t addr) {
  return addr >= cmd::BASE_ADDRESS && addr <= cmd::MAX_ADDRESS;
}

static bool isValidRegister(uint8_t reg) {
  return reg < cmd::NUM_REGISTERS;
}

static bool isValidPin(Pin pin) {
  return pin < cmd::TOTAL_PINS;
}

static bool isValidPort(Port port) {
  return port == Port::PORT_0 || port == Port::PORT_1;
}

static bool isValidDirection(Direction direction) {
  return direction == Direction::INPUT_MODE || direction == Direction::OUTPUT_MODE;
}

static bool isInputRegister(uint8_t reg) {
  return reg == cmd::REG_INPUT_PORT_0 || reg == cmd::REG_INPUT_PORT_1;
}

static uint8_t pairedRegisterAt(uint8_t startReg, size_t offset) {
  return static_cast<uint8_t>((startReg & 0xFEU) |
                              ((startReg + static_cast<uint8_t>(offset)) & 0x01U));
}

static uint8_t bitMaskForPin(Pin pin) {
  return static_cast<uint8_t>(1U << (pin % cmd::PINS_PER_PORT));
}

static Status presenceReadFailureStatus(const Status& st) {
  if (st.code == Err::I2C_NACK_ADDR) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }
  return st;
}

class ScopedOfflineI2cAllowance {
public:
  explicit ScopedOfflineI2cAllowance(bool& flag, bool allow) : _flag(flag), _old(flag) {
    _flag = allow;
  }

  ~ScopedOfflineI2cAllowance() {
    _flag = _old;
  }

  ScopedOfflineI2cAllowance(const ScopedOfflineI2cAllowance&) = delete;
  ScopedOfflineI2cAllowance& operator=(const ScopedOfflineI2cAllowance&) = delete;

private:
  bool& _flag;
  bool _old;
};

}  // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

Status PCA9555::begin(const Config& config) {
  const bool dirtyBeforeBegin = _hardwareStateDirty;
  const Status dirtyErrorBeforeBegin = _hardwareStateDirtyError;

  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _allowOfflineI2c = false;

  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _hardwareStateDirty = dirtyBeforeBegin;
  _hardwareStateDirtyError = dirtyBeforeBegin ? dirtyErrorBeforeBegin : Status::Ok();
  _finishJob(Status::Ok());
  _lastInputData = PortData{};
  _lastInputDataValid = false;
  _jobOutput0 = Config{}.outputPort0;
  _jobOutput1 = Config{}.outputPort1;
  _jobConfig0 = Config{}.configPort0;
  _jobConfig1 = Config{}.configPort1;
  _pollNowMs = 0;
  _pollNowMsActive = false;

  _cachedOutput0 = Config{}.outputPort0;
  _cachedOutput1 = Config{}.outputPort1;
  _cachedConfig0 = Config{}.configPort0;
  _cachedConfig1 = Config{}.configPort1;

  auto resetAfterFailedBegin = [this](Status failure) -> Status {
    _config = Config{};
    _initialized = false;
    _driverState = DriverState::UNINIT;
    _allowOfflineI2c = false;
    _lastOkMs = 0;
    _lastErrorMs = 0;
    _lastError = Status::Ok();
    _consecutiveFailures = 0;
    _totalFailures = 0;
    _totalSuccess = 0;
    _finishJob(Status::Ok());
    _lastInputData = PortData{};
    _lastInputDataValid = false;
    _jobOutput0 = Config{}.outputPort0;
    _jobOutput1 = Config{}.outputPort1;
    _jobConfig0 = Config{}.configPort0;
    _jobConfig1 = Config{}.configPort1;
    _pollNowMs = 0;
    _pollNowMsActive = false;
    const bool dirty = _hardwareStateDirty;
    const Status dirtyError = _hardwareStateDirtyError;
    _clearHardwareStateDirty();
    if (dirty) {
      _hardwareStateDirty = true;
      _hardwareStateDirtyError = dirtyError;
    }
    _cachedOutput0 = Config{}.outputPort0;
    _cachedOutput1 = Config{}.outputPort1;
    _cachedConfig0 = Config{}.configPort0;
    _cachedConfig1 = Config{}.configPort1;
    return failure;
  };

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if ((config.i2cLock == nullptr) != (config.i2cUnlock == nullptr)) {
    return Status::Error(Err::INVALID_CONFIG, "I2C lock/unlock callbacks must both be set");
  }
  if (config.i2cTimeoutMs == 0) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout must be > 0");
  }
  if (!isValidAddress(config.i2cAddress)) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C address (must be 0x20-0x27)");
  }

  _config = config;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }

  // Verify device presence and, by default, require POR configuration defaults.
  uint8_t configRegs[2] = {};
  uint8_t startReg = cmd::REG_CONFIG_PORT_0;
  Status st = _i2cWriteReadRaw(&startReg, 1, configRegs, sizeof(configRegs));
  if (!st.ok()) {
    return resetAfterFailedBegin(presenceReadFailureStatus(st));
  }
  if (_config.requireConfigPortDefaults &&
      (configRegs[0] != cmd::DEFAULT_CONFIG || configRegs[1] != cmd::DEFAULT_CONFIG)) {
    const int32_t detail =
        static_cast<int32_t>((static_cast<uint16_t>(configRegs[1]) << 8) | configRegs[0]);
    return resetAfterFailedBegin(
        Status::Error(Err::CONFIG_REG_MISMATCH,
                      "Configuration registers not at POR defaults",
                      detail));
  }

  st = _applyConfig();
  if (!st.ok()) {
    return resetAfterFailedBegin(st);
  }

  _initialized = true;
  _driverState = DriverState::READY;
  _clearHardwareStateDirty();

  return Status::Ok();
}

void PCA9555::tick(uint32_t nowMs) {
  (void)pollJob(nowMs, 1);
}

void PCA9555::end() {
  const bool dirtyBeforeEnd = _hardwareStateDirty;
  const Status dirtyErrorBeforeEnd = _hardwareStateDirtyError;
  bool safeStateWriteAttempted = false;
  Status safeStateWriteStatus = Status::Ok();

  if (_initialized && _driverState != DriverState::OFFLINE) {
    // Best-effort: set all pins to input (safe high-Z state).
    // Uses raw I2C to avoid health tracking during shutdown.
    const uint8_t payload[3] = {cmd::REG_CONFIG_PORT_0, 0xFF, 0xFF};
    safeStateWriteAttempted = true;
    safeStateWriteStatus = _i2cWriteRaw(payload, sizeof(payload));
  }

  _initialized = false;
  _driverState = DriverState::UNINIT;
  _cachedOutput0 = 0xFF;
  _cachedOutput1 = 0xFF;
  _cachedConfig0 = 0xFF;
  _cachedConfig1 = 0xFF;
  _config = Config{};
  _allowOfflineI2c = false;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _finishJob(Status::Ok());
  _lastInputData = PortData{};
  _lastInputDataValid = false;
  _jobOutput0 = 0xFF;
  _jobOutput1 = 0xFF;
  _jobConfig0 = 0xFF;
  _jobConfig1 = 0xFF;
  _pollNowMs = 0;
  _pollNowMsActive = false;
  if (safeStateWriteAttempted && safeStateWriteStatus.ok()) {
    _clearHardwareStateDirty();
  } else if (safeStateWriteAttempted) {
    _markHardwareStateDirty(safeStateWriteStatus);
  } else if (dirtyBeforeEnd) {
    _hardwareStateDirty = true;
    _hardwareStateDirtyError = dirtyErrorBeforeEnd;
  } else {
    _clearHardwareStateDirty();
  }
}

// ===========================================================================
// Chunked Job API
// ===========================================================================

Status PCA9555::startReadInputsJob() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  _jobType = JobType::READ_INPUTS;
  _jobStep = JobStep::READ_INPUT_PAIR;
  _lastJobStatus = Status::Error(Err::IN_PROGRESS, "Job in progress");
  return _lastJobStatus;
}

Status PCA9555::startWriteOutputsJob(uint16_t mask, uint16_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  const uint16_t cached = static_cast<uint16_t>(
      (static_cast<uint16_t>(_cachedOutput1) << 8) | _cachedOutput0);
  const uint16_t next = static_cast<uint16_t>(
      (cached & static_cast<uint16_t>(~mask)) | (value & mask));
  if (next == cached) {
    _lastJobStatus = _hardwareStateCleanStatus();
    return _lastJobStatus;
  }

  _jobType = JobType::WRITE_OUTPUTS;
  _jobStep = JobStep::WRITE_OUTPUT_PAIR;
  _jobOutput0 = static_cast<uint8_t>(next & 0xFFU);
  _jobOutput1 = static_cast<uint8_t>((next >> 8) & 0xFFU);
  _jobNeedsConfigWrite = false;
  _lastJobStatus = Status::Error(Err::IN_PROGRESS, "Job in progress");
  return _lastJobStatus;
}

Status PCA9555::startConfigureOutputsJob(uint16_t mask, uint16_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  const uint16_t cachedOutput = static_cast<uint16_t>(
      (static_cast<uint16_t>(_cachedOutput1) << 8) | _cachedOutput0);
  const uint16_t cachedConfig = static_cast<uint16_t>(
      (static_cast<uint16_t>(_cachedConfig1) << 8) | _cachedConfig0);
  const uint16_t nextOutput = static_cast<uint16_t>(
      (cachedOutput & static_cast<uint16_t>(~mask)) | (value & mask));
  const uint16_t nextConfig = static_cast<uint16_t>(
      cachedConfig & static_cast<uint16_t>(~mask));
  const uint16_t transitionMask = static_cast<uint16_t>(cachedConfig & mask);

  const bool needsOutputWrite = (nextOutput != cachedOutput) || (transitionMask != 0U);
  const bool needsConfigWrite = (nextConfig != cachedConfig);
  if (!needsOutputWrite && !needsConfigWrite) {
    _lastJobStatus = _hardwareStateCleanStatus();
    return _lastJobStatus;
  }

  _jobType = JobType::CONFIGURE_OUTPUTS;
  _jobStep = needsOutputWrite ? JobStep::PRELOAD_OUTPUT_PAIR : JobStep::WRITE_CONFIG_PAIR;
  _jobOutput0 = static_cast<uint8_t>(nextOutput & 0xFFU);
  _jobOutput1 = static_cast<uint8_t>((nextOutput >> 8) & 0xFFU);
  _jobConfig0 = static_cast<uint8_t>(nextConfig & 0xFFU);
  _jobConfig1 = static_cast<uint8_t>((nextConfig >> 8) & 0xFFU);
  _jobNeedsConfigWrite = needsConfigWrite;
  _lastJobStatus = Status::Error(Err::IN_PROGRESS, "Job in progress");
  return _lastJobStatus;
}

Status PCA9555::pollJob(uint32_t nowMs, uint8_t maxInstructions) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_jobType == JobType::NONE) {
    return Status::Ok();
  }
  if (_driverState == DriverState::OFFLINE) {
    const Status st = Status::Error(Err::BUSY, "Driver is offline; call recover()");
    _finishJob(st);
    return st;
  }
  if (maxInstructions == 0) {
    return Status::Error(Err::IN_PROGRESS, "Job in progress");
  }

  _pollNowMs = nowMs;
  _pollNowMsActive = true;

  uint8_t executed = 0;
  while (_jobType != JobType::NONE && executed < maxInstructions) {
    _jobInstructionActive = true;
    const Status st = _executeJobInstruction();
    _jobInstructionActive = false;
    ++executed;

    if (st.inProgress()) {
      _lastJobStatus = st;
      _pollNowMsActive = false;
      return st;
    }
    if (!st.ok()) {
      _pollNowMsActive = false;
      _finishJob(st);
      return st;
    }
  }

  _pollNowMsActive = false;

  if (_jobType == JobType::NONE) {
    return Status::Ok();
  }
  _lastJobStatus = Status::Error(Err::IN_PROGRESS, "Job in progress", executed);
  return _lastJobStatus;
}

bool PCA9555::jobActive() const {
  return _jobType != JobType::NONE;
}

Status PCA9555::lastJobStatus() const {
  return _lastJobStatus;
}

Status PCA9555::getLastReadInputs(PortData& data) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_lastInputDataValid) {
    return Status::Error(Err::BUSY, "No input snapshot available");
  }
  data = _lastInputData;
  return Status::Ok();
}

SettingsSnapshot PCA9555::getSettings() const {
  SettingsSnapshot snapshot;
  snapshot.config = _config;
  snapshot.state = _driverState;
  snapshot.initialized = _initialized;
  snapshot.lastOkMs = _lastOkMs;
  snapshot.lastErrorMs = _lastErrorMs;
  snapshot.lastError = _lastError;
  snapshot.consecutiveFailures = _consecutiveFailures;
  snapshot.totalFailures = _totalFailures;
  snapshot.totalSuccess = _totalSuccess;
  snapshot.hardwareStateDirty = _hardwareStateDirty;
  snapshot.hardwareStateDirtyError = _hardwareStateDirtyError;
  return snapshot;
}

Status PCA9555::getSettings(SettingsSnapshot& out) const {
  out = getSettings();
  return Status::Ok();
}

// ===========================================================================
// Diagnostics
// ===========================================================================

Status PCA9555::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  uint8_t configVal = 0;
  Status st = _readRegisterRaw(cmd::REG_CONFIG_PORT_0, configVal);
  if (!st.ok()) {
    return presenceReadFailureStatus(st);
  }

  return Status::Ok();
}

Status PCA9555::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  const bool startedOffline = (_driverState == DriverState::OFFLINE);
  ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
  Status result = [&]() -> Status {
    // Use tracked read to update health counters.
    uint8_t configVal = 0;
    Status st = readRegs(cmd::REG_CONFIG_PORT_0, &configVal, 1);
    if (!st.ok()) {
      return st;
    }

    // Re-apply configuration: after a power glitch the device registers
    // revert to defaults.
    st = _applyConfig();
    if (!st.ok()) {
      return st;
    }

    return Status::Ok();
  }();
  if (startedOffline && !result.ok()) {
    _reassertOfflineLatch();
  }
  if (result.ok()) {
    _clearHardwareStateDirty();
  }
  return result;
}

// ===========================================================================
// Input API
// ===========================================================================

Status PCA9555::readInputs(PortData& data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t buf[2] = {};
  bool readCompleted = false;
  Status st = _readInputRegistersCompound(cmd::REG_INPUT_PORT_0, buf, 2, readCompleted);
  if (readCompleted) {
    data.port0 = buf[0];
    data.port1 = buf[1];
    _lastInputData = data;
    _lastInputDataValid = true;
  }

  return st;
}

Status PCA9555::readInputsAndClearInterrupt(uint16_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t buf[2] = {};
  bool readCompleted = false;
  Status st = _readInputRegistersCompound(cmd::REG_INPUT_PORT_0, buf, 2, readCompleted);
  if (readCompleted) {
    value = static_cast<uint16_t>((static_cast<uint16_t>(buf[1]) << 8) | buf[0]);
    _lastInputData.port0 = buf[0];
    _lastInputData.port1 = buf[1];
    _lastInputDataValid = true;
  }
  return st;
}

Status PCA9555::clearInterrupts() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t buf[2] = {};
  bool readCompleted = false;
  Status st = _readInputRegistersCompound(cmd::REG_INPUT_PORT_0, buf, 2, readCompleted);
  if (readCompleted) {
    _lastInputData.port0 = buf[0];
    _lastInputData.port1 = buf[1];
    _lastInputDataValid = true;
  }
  return st;
}

Status PCA9555::applyInterruptErrataWorkaround() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  bool locked = false;
  Status st = _lockBus(locked);
  if (!st.ok()) {
    return st;
  }

  st = _applyInterruptErrataUnlocked();
  _unlockBus(locked);
  return st;
}

Status PCA9555::applyInterruptErrataWorkaroundUnlocked() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  return _applyInterruptErrataUnlocked();
}

Status PCA9555::readInput(Port port, uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPort(port)) {
    return Status::Error(Err::INVALID_PARAM, "Port out of range");
  }

  const uint8_t reg = (port == Port::PORT_0)
    ? cmd::REG_INPUT_PORT_0
    : cmd::REG_INPUT_PORT_1;

  uint8_t buf = 0;
  bool readCompleted = false;
  Status st = _readInputRegistersCompound(reg, &buf, 1, readCompleted);
  if (readCompleted) {
    value = buf;
  }
  return st;
}

Status PCA9555::readPin(Pin pin, bool& state) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }

  const Port port = (pin < cmd::PINS_PER_PORT) ? Port::PORT_0 : Port::PORT_1;
  const uint8_t reg = (port == Port::PORT_0)
    ? cmd::REG_INPUT_PORT_0
    : cmd::REG_INPUT_PORT_1;
  uint8_t value = 0;
  bool readCompleted = false;
  Status st = _readInputRegistersCompound(reg, &value, 1, readCompleted);
  if (readCompleted) {
    state = (value & bitMaskForPin(pin)) != 0;
  }
  return st;
}

// ===========================================================================
// Output API
// ===========================================================================

Status PCA9555::writeOutputs(const PortData& data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  // Burst write both output ports (auto-increment within pair)
  const uint8_t buf[2] = {data.port0, data.port1};
  Status st = writeRegs(cmd::REG_OUTPUT_PORT_0, buf, 2);
  if (!st.ok()) {
    return st;
  }

  _cachedOutput0 = data.port0;
  _cachedOutput1 = data.port1;
  _config.outputPort0 = data.port0;
  _config.outputPort1 = data.port1;
  return Status::Ok();
}

Status PCA9555::writeOutput(Port port, uint8_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPort(port)) {
    return Status::Error(Err::INVALID_PARAM, "Port out of range");
  }

  const uint8_t reg = (port == Port::PORT_0)
    ? cmd::REG_OUTPUT_PORT_0
    : cmd::REG_OUTPUT_PORT_1;

  Status st = writeRegs(reg, &value, 1);
  if (!st.ok()) {
    return st;
  }

  if (port == Port::PORT_0) {
    _cachedOutput0 = value;
    _config.outputPort0 = value;
  } else {
    _cachedOutput1 = value;
    _config.outputPort1 = value;
  }
  return Status::Ok();
}

Status PCA9555::readOutput(Port port, uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPort(port)) {
    return Status::Error(Err::INVALID_PARAM, "Port out of range");
  }

  const uint8_t reg = (port == Port::PORT_0)
    ? cmd::REG_OUTPUT_PORT_0
    : cmd::REG_OUTPUT_PORT_1;

  Status st = readRegs(reg, &value, 1);
  if (!st.ok()) {
    return st;
  }

  _syncShadowRegister(reg, value);
  return Status::Ok();
}

Status PCA9555::writePin(Pin pin, bool high) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  const uint8_t mask = bitMaskForPin(pin);
  const bool isPort0 = (pin < cmd::PINS_PER_PORT);

  uint8_t& cached = isPort0 ? _cachedOutput0 : _cachedOutput1;
  uint8_t newVal = cached;
  if (high) {
    newVal |= mask;
  } else {
    newVal &= static_cast<uint8_t>(~mask);
  }

  if (newVal == cached) {
    return _hardwareStateCleanStatus();
  }

  const Port port = isPort0 ? Port::PORT_0 : Port::PORT_1;
  return writeOutput(port, newVal);
}

Status PCA9555::readOutputPin(Pin pin, bool& high) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }

  const Port port = (pin < cmd::PINS_PER_PORT) ? Port::PORT_0 : Port::PORT_1;
  uint8_t value = 0;
  Status st = readOutput(port, value);
  if (!st.ok()) {
    return st;
  }

  high = (value & bitMaskForPin(pin)) != 0;
  return Status::Ok();
}

Status PCA9555::readOutputs(PortData& data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t buf[2] = {};
  Status st = readRegs(cmd::REG_OUTPUT_PORT_0, buf, 2);
  if (!st.ok()) {
    return st;
  }

  data.port0 = buf[0];
  data.port1 = buf[1];

  // Update cache from actual device state
  _cachedOutput0 = buf[0];
  _cachedOutput1 = buf[1];
  _config.outputPort0 = buf[0];
  _config.outputPort1 = buf[1];

  return Status::Ok();
}

Status PCA9555::preloadOutput(Pin pin, bool high) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }

  const uint16_t mask = static_cast<uint16_t>(1U << pin);
  const uint16_t values = high ? mask : 0U;
  return preloadOutputs(mask, values);
}

Status PCA9555::preloadOutputs(uint16_t mask, uint16_t values) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }
  if (mask == 0) {
    return Status::Ok();
  }

  PortData data;
  data.port0 = static_cast<uint8_t>((_cachedOutput0 & ~static_cast<uint8_t>(mask & 0xFFU)) |
      (static_cast<uint8_t>(values) & static_cast<uint8_t>(mask)));
  data.port1 = static_cast<uint8_t>((_cachedOutput1 &
      ~static_cast<uint8_t>((mask >> 8) & 0xFFU)) |
      (static_cast<uint8_t>(values >> 8) & static_cast<uint8_t>(mask >> 8)));

  const uint8_t buf[2] = {data.port0, data.port1};
  Status st = writeRegs(cmd::REG_OUTPUT_PORT_0, buf, 2);
  if (!st.ok()) {
    return st;
  }

  _cachedOutput0 = data.port0;
  _cachedOutput1 = data.port1;
  _config.outputPort0 = data.port0;
  _config.outputPort1 = data.port1;
  return Status::Ok();
}

// ===========================================================================
// Bit Manipulation API
// ===========================================================================

Status PCA9555::setOutputBits(uint16_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  PortData data;
  data.port0 = _cachedOutput0 | static_cast<uint8_t>(mask & 0xFF);
  data.port1 = _cachedOutput1 | static_cast<uint8_t>((mask >> 8) & 0xFF);

  if (data.port0 == _cachedOutput0 && data.port1 == _cachedOutput1) {
    return _hardwareStateCleanStatus();
  }

  return writeOutputs(data);
}

Status PCA9555::clearOutputBits(uint16_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  PortData data;
  data.port0 = _cachedOutput0 & static_cast<uint8_t>(~mask & 0xFF);
  data.port1 = _cachedOutput1 & static_cast<uint8_t>(~(mask >> 8) & 0xFF);

  if (data.port0 == _cachedOutput0 && data.port1 == _cachedOutput1) {
    return _hardwareStateCleanStatus();
  }

  return writeOutputs(data);
}

Status PCA9555::toggleOutputBits(uint16_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  if (mask == 0) {
    return Status::Ok();
  }

  PortData data;
  data.port0 = _cachedOutput0 ^ static_cast<uint8_t>(mask & 0xFF);
  data.port1 = _cachedOutput1 ^ static_cast<uint8_t>((mask >> 8) & 0xFF);

  return writeOutputs(data);
}

Status PCA9555::togglePin(Pin pin) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  const uint8_t mask = bitMaskForPin(pin);
  const bool isPort0 = (pin < cmd::PINS_PER_PORT);
  const uint8_t& cached = isPort0 ? _cachedOutput0 : _cachedOutput1;
  const uint8_t newVal = cached ^ mask;

  const Port port = isPort0 ? Port::PORT_0 : Port::PORT_1;
  return writeOutput(port, newVal);
}

Status PCA9555::configureInputBits(uint16_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  PortData data;
  data.port0 = _cachedConfig0 | static_cast<uint8_t>(mask & 0xFF);
  data.port1 = _cachedConfig1 | static_cast<uint8_t>((mask >> 8) & 0xFF);

  if (data.port0 == _cachedConfig0 && data.port1 == _cachedConfig1) {
    return _hardwareStateCleanStatus();
  }

  return setConfiguration(data);
}

Status PCA9555::configureOutputBits(uint16_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  const uint16_t currentConfig = static_cast<uint16_t>(
      (static_cast<uint16_t>(_cachedConfig1) << 8) | _cachedConfig0);
  const uint16_t transitionMask = static_cast<uint16_t>(currentConfig & mask);
  if (transitionMask == 0) {
    return _hardwareStateCleanStatus();
  }

  const uint16_t cachedOutputs = static_cast<uint16_t>(
      (static_cast<uint16_t>(_cachedOutput1) << 8) | _cachedOutput0);
  return configureOutputs(transitionMask, cachedOutputs);
}

Status PCA9555::setInvertBits(uint16_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  PortData data;
  data.port0 = _config.polarityPort0 | static_cast<uint8_t>(mask & 0xFF);
  data.port1 = _config.polarityPort1 | static_cast<uint8_t>((mask >> 8) & 0xFF);

  if (data.port0 == _config.polarityPort0 &&
      data.port1 == _config.polarityPort1) {
    return _hardwareStateCleanStatus();
  }

  return setPolarity(data);
}

Status PCA9555::clearInvertBits(uint16_t mask) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  PortData data;
  data.port0 = _config.polarityPort0 & static_cast<uint8_t>(~mask & 0xFF);
  data.port1 = _config.polarityPort1 & static_cast<uint8_t>(~(mask >> 8) & 0xFF);

  if (data.port0 == _config.polarityPort0 &&
      data.port1 == _config.polarityPort1) {
    return _hardwareStateCleanStatus();
  }

  return setPolarity(data);
}

// ===========================================================================
// Configuration API
// ===========================================================================

Status PCA9555::setConfiguration(const PortData& data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  const uint16_t inputToOutputMask = static_cast<uint16_t>(
      ((static_cast<uint16_t>(_cachedConfig1) << 8) | _cachedConfig0) &
      ~((static_cast<uint16_t>(data.port1) << 8) | data.port0));
  if (inputToOutputMask != 0) {
    const uint16_t cachedOutputs = static_cast<uint16_t>(
        (static_cast<uint16_t>(_cachedOutput1) << 8) | _cachedOutput0);
    Status st = preloadOutputs(inputToOutputMask, cachedOutputs);
    if (!st.ok()) {
      return st;
    }
  }

  return _writeConfigurationNoPreload(data);
}

Status PCA9555::_writeConfigurationNoPreload(const PortData& data) {
  const uint8_t buf[2] = {data.port0, data.port1};
  Status st = writeRegs(cmd::REG_CONFIG_PORT_0, buf, 2);
  if (!st.ok()) {
    return st;
  }

  _cachedConfig0 = data.port0;
  _cachedConfig1 = data.port1;
  _config.configPort0 = data.port0;
  _config.configPort1 = data.port1;
  return Status::Ok();
}

Status PCA9555::setPortConfiguration(Port port, uint8_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPort(port)) {
    return Status::Error(Err::INVALID_PARAM, "Port out of range");
  }

  const uint8_t currentConfig = (port == Port::PORT_0) ? _cachedConfig0 : _cachedConfig1;
  const uint8_t inputToOutputMask = static_cast<uint8_t>(currentConfig & ~value);
  if (inputToOutputMask != 0) {
    const uint16_t mask = (port == Port::PORT_0)
        ? inputToOutputMask
        : static_cast<uint16_t>(inputToOutputMask) << 8;
    const uint16_t cachedOutputs = static_cast<uint16_t>(
        (static_cast<uint16_t>(_cachedOutput1) << 8) | _cachedOutput0);
    Status st = preloadOutputs(mask, cachedOutputs);
    if (!st.ok()) {
      return st;
    }
  }

  return _writePortConfigurationNoPreload(port, value);
}

Status PCA9555::_writePortConfigurationNoPreload(Port port, uint8_t value) {
  const uint8_t reg = (port == Port::PORT_0)
    ? cmd::REG_CONFIG_PORT_0
    : cmd::REG_CONFIG_PORT_1;

  Status st = writeRegs(reg, &value, 1);
  if (!st.ok()) {
    return st;
  }

  if (port == Port::PORT_0) {
    _cachedConfig0 = value;
    _config.configPort0 = value;
  } else {
    _cachedConfig1 = value;
    _config.configPort1 = value;
  }
  return Status::Ok();
}

Status PCA9555::getPortConfiguration(Port port, uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPort(port)) {
    return Status::Error(Err::INVALID_PARAM, "Port out of range");
  }

  const uint8_t reg = (port == Port::PORT_0)
    ? cmd::REG_CONFIG_PORT_0
    : cmd::REG_CONFIG_PORT_1;

  Status st = readRegs(reg, &value, 1);
  if (!st.ok()) {
    return st;
  }

  _syncShadowRegister(reg, value);
  return Status::Ok();
}

Status PCA9555::getConfiguration(PortData& data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t buf[2] = {};
  Status st = readRegs(cmd::REG_CONFIG_PORT_0, buf, 2);
  if (!st.ok()) {
    return st;
  }

  data.port0 = buf[0];
  data.port1 = buf[1];

  _cachedConfig0 = buf[0];
  _cachedConfig1 = buf[1];
  _config.configPort0 = buf[0];
  _config.configPort1 = buf[1];

  return Status::Ok();
}

Status PCA9555::setPolarity(const PortData& data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  const uint8_t buf[2] = {data.port0, data.port1};
  Status st = writeRegs(cmd::REG_POLARITY_INV_0, buf, 2);
  if (!st.ok()) {
    return st;
  }

  _config.polarityPort0 = data.port0;
  _config.polarityPort1 = data.port1;
  return Status::Ok();
}

Status PCA9555::setPortPolarity(Port port, uint8_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPort(port)) {
    return Status::Error(Err::INVALID_PARAM, "Port out of range");
  }

  const uint8_t reg = (port == Port::PORT_0)
    ? cmd::REG_POLARITY_INV_0
    : cmd::REG_POLARITY_INV_1;

  Status st = writeRegs(reg, &value, 1);
  if (!st.ok()) {
    return st;
  }

  if (port == Port::PORT_0) {
    _config.polarityPort0 = value;
  } else {
    _config.polarityPort1 = value;
  }
  return Status::Ok();
}

Status PCA9555::getPortPolarity(Port port, uint8_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPort(port)) {
    return Status::Error(Err::INVALID_PARAM, "Port out of range");
  }

  const uint8_t reg = (port == Port::PORT_0)
    ? cmd::REG_POLARITY_INV_0
    : cmd::REG_POLARITY_INV_1;

  Status st = readRegs(reg, &value, 1);
  if (!st.ok()) {
    return st;
  }

  _syncShadowRegister(reg, value);
  return Status::Ok();
}

Status PCA9555::getPolarity(PortData& data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint8_t buf[2] = {};
  Status st = readRegs(cmd::REG_POLARITY_INV_0, buf, 2);
  if (!st.ok()) {
    return st;
  }

  data.port0 = buf[0];
  data.port1 = buf[1];
  _config.polarityPort0 = buf[0];
  _config.polarityPort1 = buf[1];
  return Status::Ok();
}

Status PCA9555::setPinPolarity(Pin pin, bool inverted) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  const uint8_t mask = bitMaskForPin(pin);
  const bool isPort0 = (pin < cmd::PINS_PER_PORT);

  uint8_t current = isPort0 ? _config.polarityPort0 : _config.polarityPort1;
  uint8_t newVal = current;
  if (inverted) {
    newVal |= mask;
  } else {
    newVal &= static_cast<uint8_t>(~mask);
  }

  if (newVal == current) {
    return _hardwareStateCleanStatus();
  }

  const Port port = isPort0 ? Port::PORT_0 : Port::PORT_1;
  return setPortPolarity(port, newVal);
}

Status PCA9555::getPinPolarity(Pin pin, bool& inverted) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }

  const Port port = (pin < cmd::PINS_PER_PORT) ? Port::PORT_0 : Port::PORT_1;
  uint8_t value = 0;
  Status st = getPortPolarity(port, value);
  if (!st.ok()) {
    return st;
  }

  inverted = (value & bitMaskForPin(pin)) != 0;
  return Status::Ok();
}

Status PCA9555::setPinDirection(Pin pin, bool input) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  const uint8_t mask = bitMaskForPin(pin);
  const bool isPort0 = (pin < cmd::PINS_PER_PORT);

  uint8_t& cached = isPort0 ? _cachedConfig0 : _cachedConfig1;
  uint8_t newVal = cached;
  if (input) {
    newVal |= mask;
  } else {
    newVal &= static_cast<uint8_t>(~mask);
  }

  if (newVal == cached) {
    return _hardwareStateCleanStatus();
  }

  const Port port = isPort0 ? Port::PORT_0 : Port::PORT_1;
  return setPortConfiguration(port, newVal);
}

Status PCA9555::setDirection(Pin pin, Direction direction) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }
  if (!isValidDirection(direction)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid direction");
  }

  return setPinDirection(pin, direction == Direction::INPUT_MODE);
}

Status PCA9555::configureOutputs(uint16_t outputMask, uint16_t outputValues) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }
  if (outputMask == 0) {
    return Status::Ok();
  }

  Status st = preloadOutputs(outputMask, outputValues);
  if (!st.ok()) {
    return st;
  }

  PortData data;
  data.port0 = static_cast<uint8_t>(_cachedConfig0 &
      ~static_cast<uint8_t>(outputMask & 0xFFU));
  data.port1 = static_cast<uint8_t>(_cachedConfig1 &
      ~static_cast<uint8_t>((outputMask >> 8) & 0xFFU));

  if (data.port0 == _cachedConfig0 && data.port1 == _cachedConfig1) {
    return _hardwareStateCleanStatus();
  }

  return _writeConfigurationNoPreload(data);
}

Status PCA9555::getPinDirection(Pin pin, bool& input) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }

  const Port port = (pin < cmd::PINS_PER_PORT) ? Port::PORT_0 : Port::PORT_1;
  uint8_t value = 0;
  Status st = getPortConfiguration(port, value);
  if (!st.ok()) {
    return st;
  }

  input = (value & bitMaskForPin(pin)) != 0;
  return Status::Ok();
}

// ===========================================================================
// Register Access (Public)
// ===========================================================================

Status PCA9555::readRegister(uint8_t reg, uint8_t& value) {
  return readRegisters(reg, &value, 1);
}

Status PCA9555::readRegisters(uint8_t startReg, uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (!isValidRegister(startReg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address out of range");
  }
  if (len > MAX_BULK_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Read length too large");
  }

  Status st = Status::Ok();
  if (isInputRegister(startReg)) {
    bool readCompleted = false;
    st = _readInputRegistersCompound(startReg, buf, len, readCompleted);
  } else {
    st = readRegs(startReg, buf, len);
  }
  if (!st.ok()) {
    return st;
  }

  for (size_t i = 0; i < len; ++i) {
    _syncShadowRegister(pairedRegisterAt(startReg, i), buf[i]);
  }

  return Status::Ok();
}

Status PCA9555::writeRegister(uint8_t reg, uint8_t value) {
  return writeRegisters(reg, &value, 1);
}

Status PCA9555::writeRegisters(uint8_t startReg, const uint8_t* buf, size_t len) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (startReg < cmd::REG_OUTPUT_PORT_0 || startReg > cmd::REG_CONFIG_PORT_1) {
    return Status::Error(Err::INVALID_PARAM, "Register not writable or out of range");
  }
  Status st = writeRegs(startReg, buf, len);
  if (!st.ok()) {
    return st;
  }

  for (size_t i = 0; i < len; ++i) {
    _syncShadowRegister(pairedRegisterAt(startReg, i), buf[i]);
  }
  return Status::Ok();
}

// ===========================================================================
// Transport Wrappers
// ===========================================================================

Status PCA9555::_i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                                 uint8_t* rxBuf, size_t rxLen) {
  if (txBuf == nullptr || txLen == 0 || rxBuf == nullptr || rxLen == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write-read not set");
  }
  return _config.i2cWriteRead(_config.i2cAddress, txBuf, txLen, rxBuf, rxLen,
                              _config.i2cTimeoutMs, _config.i2cUser);
}

Status PCA9555::_i2cWriteRaw(const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  return _config.i2cWrite(_config.i2cAddress, buf, len, _config.i2cTimeoutMs,
                          _config.i2cUser);
}

Status PCA9555::_i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                                     uint8_t* rxBuf, size_t rxLen) {
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  if (txBuf == nullptr || txLen == 0 || rxBuf == nullptr || rxLen == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status PCA9555::_i2cWriteTracked(const uint8_t* buf, size_t len) {
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteRaw(buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  if (len > 1 && !st.ok() && !st.inProgress()) {
    _markHardwareStateDirty(st);
  }
  return _updateHealth(st);
}

// ===========================================================================
// Register Access (Internal)
// ===========================================================================

Status PCA9555::readRegs(uint8_t startReg, uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (!isValidRegister(startReg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address out of range");
  }
  if (len > MAX_BULK_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Read length too large");
  }
  if (_jobType != JobType::NONE && !_jobInstructionActive) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  uint8_t reg = startReg;
  return _i2cWriteReadTracked(&reg, 1, buf, len);
}

Status PCA9555::writeRegs(uint8_t startReg, const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (!isValidRegister(startReg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address out of range");
  }
  if (len > MAX_BULK_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Write length too large");
  }
  if (_jobType != JobType::NONE && !_jobInstructionActive) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  uint8_t payload[MAX_BULK_LEN + 1] = {};
  payload[0] = startReg;
  std::memcpy(&payload[1], buf, len);

  return _i2cWriteTracked(payload, len + 1);
}

Status PCA9555::_readInputRegistersLocked(uint8_t startReg, uint8_t* buf, size_t len,
                                          bool& readCompleted) {
  readCompleted = false;
  Status st = readRegs(startReg, buf, len);
  if (!st.ok()) {
    return st;
  }

  readCompleted = true;
  if (_config.applyInterruptErrata) {
    st = _applyInterruptErrataUnlocked();
    if (!st.ok()) {
      return st;
    }
  }

  return Status::Ok();
}

Status PCA9555::_readInputRegistersCompound(uint8_t startReg, uint8_t* buf, size_t len,
                                            bool& readCompleted) {
  readCompleted = false;
  if (_jobType != JobType::NONE && !_jobInstructionActive) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }
  Status availability = _normalOperationStatus();
  if (!availability.ok()) {
    return availability;
  }

  bool locked = false;
  if (_config.applyInterruptErrata) {
    Status st = _lockBus(locked);
    if (!st.ok()) {
      return st;
    }
  }

  Status st = _readInputRegistersLocked(startReg, buf, len, readCompleted);
  _unlockBus(locked);
  return st;
}

Status PCA9555::_readRegisterRaw(uint8_t reg, uint8_t& value) {
  uint8_t addr = reg;
  return _i2cWriteReadRaw(&addr, 1, &value, 1);
}

// ===========================================================================
// Health Management
// ===========================================================================

Status PCA9555::_updateHealth(const Status& st) {
  if (!_initialized) {
    return st;
  }
  if (st.inProgress()) {
    return st;
  }

  const uint32_t now = _nowMs();
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

  if (st.ok()) {
    _lastOkMs = now;
    if (_totalSuccess < maxU32) {
      _totalSuccess++;
    }
    _consecutiveFailures = 0;
    _driverState = DriverState::READY;
    return st;
  }

  _lastError = st;
  _lastErrorMs = now;
  if (_totalFailures < maxU32) {
    _totalFailures++;
  }
  if (_consecutiveFailures < maxU8) {
    _consecutiveFailures++;
  }

  if (_consecutiveFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }

  return st;
}

Status PCA9555::_normalOperationStatus() const {
  if (_initialized && _driverState == DriverState::OFFLINE && !_allowOfflineI2c) {
    return Status::Error(Err::BUSY, "Driver is offline; call recover()");
  }
  return Status::Ok();
}

Status PCA9555::_hardwareStateCleanStatus() const {
  if (_hardwareStateDirty) {
    return Status::Error(
        Err::BUSY, "Hardware state dirty; call recover()", _hardwareStateDirtyError.detail);
  }
  return Status::Ok();
}

void PCA9555::_reassertOfflineLatch() {
  _driverState = DriverState::OFFLINE;
  const uint8_t threshold = _config.offlineThreshold == 0 ? 1 : _config.offlineThreshold;
  if (_consecutiveFailures < threshold) {
    _consecutiveFailures = threshold;
  }
}

// ===========================================================================
// Internal Helpers
// ===========================================================================

Status PCA9555::_applyConfig() {
  // Step 1: Set output values BEFORE configuring direction to avoid glitches.
  const uint8_t outBuf[2] = {_config.outputPort0, _config.outputPort1};
  Status st = writeRegs(cmd::REG_OUTPUT_PORT_0, outBuf, 2);
  if (!st.ok()) {
    return st;
  }

  // Step 2: Set polarity inversion.
  const uint8_t polBuf[2] = {_config.polarityPort0, _config.polarityPort1};
  st = writeRegs(cmd::REG_POLARITY_INV_0, polBuf, 2);
  if (!st.ok()) {
    return st;
  }

  // Step 3: Set pin directions.
  const uint8_t cfgBuf[2] = {_config.configPort0, _config.configPort1};
  st = writeRegs(cmd::REG_CONFIG_PORT_0, cfgBuf, 2);
  if (!st.ok()) {
    return st;
  }

  // Step 4/5: Read input ports to clear any pending interrupts, then apply
  // the errata workaround while holding any configured compound-sequence lock.
  uint8_t inputBuf[2] = {};
  bool readCompleted = false;
  st = _readInputRegistersCompound(cmd::REG_INPUT_PORT_0, inputBuf, 2, readCompleted);
  if (!st.ok()) {
    return st;
  }

  // Update cached state
  _cachedOutput0 = _config.outputPort0;
  _cachedOutput1 = _config.outputPort1;
  _cachedConfig0 = _config.configPort0;
  _cachedConfig1 = _config.configPort1;

  return Status::Ok();
}

Status PCA9555::_applyInterruptErrataUnlocked() {
  // Write a command byte != 0x00 to prevent false interrupt de-assertion
  // when another device on the bus is read.
  // We send just the command byte (register pointer) without data.
  const uint8_t safeCmd = cmd::ERRATA_SAFE_CMD;
  return _i2cWriteTracked(&safeCmd, 1);
}

Status PCA9555::_lockBus(bool& locked) {
  locked = false;
  if (_config.i2cLock == nullptr && _config.i2cUnlock == nullptr) {
    return Status::Ok();
  }
  if (_config.i2cLock == nullptr || _config.i2cUnlock == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C lock/unlock callbacks must both be set");
  }

  Status st = _config.i2cLock(_config.lockUser, _config.i2cTimeoutMs);
  if (!st.ok()) {
    return st;
  }

  locked = true;
  return Status::Ok();
}

void PCA9555::_unlockBus(bool locked) {
  if (locked && _config.i2cUnlock != nullptr) {
    _config.i2cUnlock(_config.lockUser);
  }
}

Status PCA9555::_executeJobInstruction() {
  switch (_jobStep) {
    case JobStep::READ_INPUT_PAIR: {
      uint8_t buf[2] = {};
      Status st = readRegs(cmd::REG_INPUT_PORT_0, buf, 2);
      if (!st.ok()) {
        return st;
      }

      _lastInputData.port0 = buf[0];
      _lastInputData.port1 = buf[1];
      _lastInputDataValid = true;
      if (_config.applyInterruptErrata) {
        _jobStep = JobStep::POINTER_PARK;
      } else {
        _finishJob(Status::Ok());
      }
      return Status::Ok();
    }

    case JobStep::POINTER_PARK: {
      Status st = _applyInterruptErrataUnlocked();
      if (!st.ok()) {
        return st;
      }
      _finishJob(Status::Ok());
      return Status::Ok();
    }

    case JobStep::WRITE_OUTPUT_PAIR: {
      const uint8_t buf[2] = {_jobOutput0, _jobOutput1};
      Status st = writeRegs(cmd::REG_OUTPUT_PORT_0, buf, 2);
      if (!st.ok()) {
        return st;
      }

      _cachedOutput0 = _jobOutput0;
      _cachedOutput1 = _jobOutput1;
      _config.outputPort0 = _jobOutput0;
      _config.outputPort1 = _jobOutput1;
      _finishJob(Status::Ok());
      return Status::Ok();
    }

    case JobStep::PRELOAD_OUTPUT_PAIR: {
      const uint8_t buf[2] = {_jobOutput0, _jobOutput1};
      Status st = writeRegs(cmd::REG_OUTPUT_PORT_0, buf, 2);
      if (!st.ok()) {
        return st;
      }

      _cachedOutput0 = _jobOutput0;
      _cachedOutput1 = _jobOutput1;
      _config.outputPort0 = _jobOutput0;
      _config.outputPort1 = _jobOutput1;
      if (_jobNeedsConfigWrite) {
        _jobStep = JobStep::WRITE_CONFIG_PAIR;
      } else {
        _finishJob(Status::Ok());
      }
      return Status::Ok();
    }

    case JobStep::WRITE_CONFIG_PAIR: {
      const uint8_t buf[2] = {_jobConfig0, _jobConfig1};
      Status st = writeRegs(cmd::REG_CONFIG_PORT_0, buf, 2);
      if (!st.ok()) {
        return st;
      }

      _cachedConfig0 = _jobConfig0;
      _cachedConfig1 = _jobConfig1;
      _config.configPort0 = _jobConfig0;
      _config.configPort1 = _jobConfig1;
      _finishJob(Status::Ok());
      return Status::Ok();
    }

    case JobStep::NONE:
    default:
      _finishJob(Status::Ok());
      return Status::Ok();
  }
}

void PCA9555::_finishJob(const Status& st) {
  _jobType = JobType::NONE;
  _jobStep = JobStep::NONE;
  _jobNeedsConfigWrite = false;
  _jobInstructionActive = false;
  _lastJobStatus = st;
}

void PCA9555::_syncShadowRegister(uint8_t reg, uint8_t value) {
  switch (reg) {
    case cmd::REG_OUTPUT_PORT_0:
      _cachedOutput0 = value;
      _config.outputPort0 = value;
      break;
    case cmd::REG_OUTPUT_PORT_1:
      _cachedOutput1 = value;
      _config.outputPort1 = value;
      break;
    case cmd::REG_POLARITY_INV_0:
      _config.polarityPort0 = value;
      break;
    case cmd::REG_POLARITY_INV_1:
      _config.polarityPort1 = value;
      break;
    case cmd::REG_CONFIG_PORT_0:
      _cachedConfig0 = value;
      _config.configPort0 = value;
      break;
    case cmd::REG_CONFIG_PORT_1:
      _cachedConfig1 = value;
      _config.configPort1 = value;
      break;
    default:
      break;
  }
}

uint32_t PCA9555::_nowMs() const {
  if (_pollNowMsActive) {
    return _pollNowMs;
  }
  if (_config.nowMs != nullptr) {
    return _config.nowMs(_config.timeUser);
  }
  return 0U;
}

void PCA9555::_markHardwareStateDirty(const Status& st) {
  if (st.ok() || st.inProgress() ||
      st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM ||
      st.code == Err::NOT_INITIALIZED || st.code == Err::BUSY) {
    return;
  }
  _hardwareStateDirty = true;
  _hardwareStateDirtyError = st;
}

void PCA9555::_clearHardwareStateDirty() {
  _hardwareStateDirty = false;
  _hardwareStateDirtyError = Status::Ok();
}

} // namespace PCA9555

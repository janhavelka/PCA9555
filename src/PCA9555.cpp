/**
 * @file PCA9555.cpp
 * @brief PCA9555 16-bit I/O expander driver implementation.
 */

#include "PCA9555/PCA9555.h"

#include <Arduino.h>
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

static bool isInputRegister(uint8_t reg) {
  return reg == cmd::REG_INPUT_PORT_0 || reg == cmd::REG_INPUT_PORT_1;
}

static uint8_t bitMaskForPin(Pin pin) {
  return static_cast<uint8_t>(1U << (pin % cmd::PINS_PER_PORT));
}

}  // namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

Status PCA9555::begin(const Config& config) {
  _initialized = false;
  _driverState = DriverState::UNINIT;

  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _allowOfflineIo = false;
  _outputDirty = false;
  _polarityDirty = false;
  _configDirty = false;

  _jobType = JobType::NONE;
  _jobStep = JobStep::NONE;
  _lastJobStatus = Status::Ok();
  _lastInputData = PortData{};
  _jobOutput0 = config.outputPort0;
  _jobOutput1 = config.outputPort1;
  _jobConfig0 = config.configPort0;
  _jobConfig1 = config.configPort1;
  _jobNeedsConfigWrite = false;
  _jobInstructionActive = false;
  _pollNowMs = 0;
  _pollNowMsActive = false;

  _cachedOutput0 = config.outputPort0;
  _cachedOutput1 = config.outputPort1;
  _cachedConfig0 = config.configPort0;
  _cachedConfig1 = config.configPort1;

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if ((config.i2cLock == nullptr) != (config.i2cUnlock == nullptr)) {
    return Status::Error(Err::INVALID_CONFIG, "I2C lock/unlock callbacks must be paired");
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
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }
  if (_config.requireConfigPortDefaults &&
      (configRegs[0] != cmd::DEFAULT_CONFIG || configRegs[1] != cmd::DEFAULT_CONFIG)) {
    const int32_t detail =
        static_cast<int32_t>((static_cast<uint16_t>(configRegs[1]) << 8) | configRegs[0]);
    return Status::Error(Err::CONFIG_REG_MISMATCH,
                         "Configuration registers not at POR defaults",
                         detail);
  }

  st = _applyConfig();
  if (!st.ok()) {
    return st;
  }

  _initialized = true;
  _driverState = DriverState::READY;

  return Status::Ok();
}

void PCA9555::tick(uint32_t nowMs) {
  (void)pollJob(nowMs, 1);
}

void PCA9555::end() {
  if (_initialized) {
    // Best-effort: set all pins to input (safe high-Z state).
    // Uses raw I2C to avoid health tracking during shutdown.
    const uint8_t payload[3] = {cmd::REG_CONFIG_PORT_0, 0xFF, 0xFF};
    (void)_i2cWriteRaw(payload, sizeof(payload));
  }

  _initialized = false;
  _driverState = DriverState::UNINIT;
  _finishJob(Status::Error(Err::NOT_INITIALIZED, "begin() not called"));
  _cachedOutput0 = 0xFF;
  _cachedOutput1 = 0xFF;
  _cachedConfig0 = 0xFF;
  _cachedConfig1 = 0xFF;
  _allowOfflineIo = false;
  _outputDirty = false;
  _polarityDirty = false;
  _configDirty = false;
}

// ===========================================================================
// Chunked Job API
// ===========================================================================

Status PCA9555::startReadInputsJob() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_driverState == DriverState::OFFLINE) {
    return Status::Error(Err::OFFLINE, "Driver offline; call recover()");
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
  if (_driverState == DriverState::OFFLINE) {
    return Status::Error(Err::OFFLINE, "Driver offline; call recover()");
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  const uint16_t cached = static_cast<uint16_t>(
      (static_cast<uint16_t>(_cachedOutput1) << 8) | _cachedOutput0);
  const uint16_t next = static_cast<uint16_t>((cached & ~mask) | (value & mask));
  if (next == cached && !_outputDirty) {
    _lastJobStatus = Status::Ok();
    return Status::Ok();
  }

  _jobType = JobType::WRITE_OUTPUTS;
  _jobStep = JobStep::WRITE_OUTPUT_PAIR;
  _jobOutput0 = static_cast<uint8_t>(next & 0xFF);
  _jobOutput1 = static_cast<uint8_t>((next >> 8) & 0xFF);
  _jobNeedsConfigWrite = false;
  _lastJobStatus = Status::Error(Err::IN_PROGRESS, "Job in progress");
  return _lastJobStatus;
}

Status PCA9555::startConfigureOutputsJob(uint16_t mask, uint16_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_driverState == DriverState::OFFLINE) {
    return Status::Error(Err::OFFLINE, "Driver offline; call recover()");
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job already active");
  }

  const uint16_t cachedOutput = static_cast<uint16_t>(
      (static_cast<uint16_t>(_cachedOutput1) << 8) | _cachedOutput0);
  const uint16_t cachedConfig = static_cast<uint16_t>(
      (static_cast<uint16_t>(_cachedConfig1) << 8) | _cachedConfig0);
  const uint16_t nextOutput = static_cast<uint16_t>((cachedOutput & ~mask) | (value & mask));
  const uint16_t nextConfig = static_cast<uint16_t>(cachedConfig & ~mask);

  const bool needsOutputWrite = (nextOutput != cachedOutput) || _outputDirty;
  const bool needsConfigWrite = (nextConfig != cachedConfig) || _configDirty;
  if (!needsOutputWrite && !needsConfigWrite) {
    _lastJobStatus = Status::Ok();
    return Status::Ok();
  }

  _jobType = JobType::CONFIGURE_OUTPUTS;
  _jobStep = needsOutputWrite ? JobStep::PRELOAD_OUTPUT_PAIR : JobStep::WRITE_CONFIG_PAIR;
  _jobOutput0 = static_cast<uint8_t>(nextOutput & 0xFF);
  _jobOutput1 = static_cast<uint8_t>((nextOutput >> 8) & 0xFF);
  _jobConfig0 = static_cast<uint8_t>(nextConfig & 0xFF);
  _jobConfig1 = static_cast<uint8_t>((nextConfig >> 8) & 0xFF);
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
    const Status st = Status::Error(Err::OFFLINE, "Driver offline; call recover()");
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
  return Status::Error(Err::IN_PROGRESS, "Job in progress", executed);
}

bool PCA9555::jobActive() const {
  return _jobType != JobType::NONE;
}

Status PCA9555::lastJobStatus() const {
  return _lastJobStatus;
}

Status PCA9555::getLastReadInputs(PortData& data) const {
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
  snapshot.outputDirty = _outputDirty;
  snapshot.polarityDirty = _polarityDirty;
  snapshot.configDirty = _configDirty;
  return snapshot;
}

// ===========================================================================
// Diagnostics
// ===========================================================================

Status PCA9555::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }
  if (_jobType != JobType::NONE) {
    return Status::Error(Err::BUSY, "Chunked job active");
  }

  uint8_t configVal = 0;
  Status st = _readRegisterRaw(cmd::REG_CONFIG_PORT_0, configVal);
  if (!st.ok()) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
  }

  return Status::Ok();
}

Status PCA9555::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  const bool previousAllowOfflineIo = _allowOfflineIo;
  _allowOfflineIo = true;

  // Use tracked read to update health counters
  uint8_t configVal = 0;
  Status st = readRegs(cmd::REG_CONFIG_PORT_0, &configVal, 1);
  if (!st.ok()) {
    _allowOfflineIo = previousAllowOfflineIo;
    return st;
  }

  // Re-apply configuration: after a power glitch the device registers
  // revert to defaults.
  st = _applyConfig();
  if (!st.ok()) {
    _allowOfflineIo = previousAllowOfflineIo;
    return st;
  }

  _allowOfflineIo = previousAllowOfflineIo;
  return Status::Ok();
}

Status PCA9555::applyInterruptErrataWorkaround() {
  Status st = _requireReadyForPublicIo();
  if (!st.ok()) {
    return st;
  }

  st = _lockI2c();
  if (!st.ok()) {
    return st;
  }

  const Status ioStatus = _applyInterruptErrata();
  _unlockI2c();
  return ioStatus;
}

Status PCA9555::applyInterruptErrataWorkaroundUnlocked() {
  Status st = _requireReadyForPublicIo();
  if (!st.ok()) {
    return st;
  }

  return _applyInterruptErrata();
}

// ===========================================================================
// Input API
// ===========================================================================

Status PCA9555::readInputs(PortData& data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  // Burst read both input ports (auto-increment within pair)
  uint8_t buf[2] = {};
  Status st = readRegs(cmd::REG_INPUT_PORT_0, buf, 2);
  if (!st.ok()) {
    return st;
  }

  data.port0 = buf[0];
  data.port1 = buf[1];
  _lastInputData = data;

  // Interrupt errata workaround
  if (_config.applyInterruptErrata) {
    st = _applyInterruptErrata();
    if (!st.ok()) {
      return st;
    }
  }

  return Status::Ok();
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

  Status st = readRegs(reg, &value, 1);
  if (!st.ok()) {
    return st;
  }

  // Interrupt errata workaround
  if (_config.applyInterruptErrata) {
    st = _applyInterruptErrata();
    if (!st.ok()) {
      return st;
    }
  }

  return Status::Ok();
}

Status PCA9555::readPin(Pin pin, bool& state) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
  }

  const Port port = (pin < cmd::PINS_PER_PORT) ? Port::PORT_0 : Port::PORT_1;
  uint8_t value = 0;
  Status st = readInput(port, value);
  if (!st.ok()) {
    return st;
  }

  state = (value & bitMaskForPin(pin)) != 0;
  return Status::Ok();
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
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
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
    if (_outputDirty) {
      return writeOutputs(PortData::fromCombined(
          static_cast<uint16_t>((static_cast<uint16_t>(_cachedOutput1) << 8) |
                                _cachedOutput0)));
    }
    return Status::Ok();
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
  _clearDirtyForRegisterPair(cmd::REG_OUTPUT_PORT_0, 2);

  return Status::Ok();
}

// ===========================================================================
// Bit Manipulation API
// ===========================================================================

Status PCA9555::setOutputBits(uint16_t mask) {
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }

  PortData data;
  data.port0 = _cachedOutput0 | static_cast<uint8_t>(mask & 0xFF);
  data.port1 = _cachedOutput1 | static_cast<uint8_t>((mask >> 8) & 0xFF);

  if (data.port0 == _cachedOutput0 && data.port1 == _cachedOutput1) {
    if (_outputDirty) {
      return writeOutputs(data);
    }
    return Status::Ok();
  }

  return writeOutputs(data);
}

Status PCA9555::clearOutputBits(uint16_t mask) {
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }

  PortData data;
  data.port0 = _cachedOutput0 & static_cast<uint8_t>(~mask & 0xFF);
  data.port1 = _cachedOutput1 & static_cast<uint8_t>(~(mask >> 8) & 0xFF);

  if (data.port0 == _cachedOutput0 && data.port1 == _cachedOutput1) {
    if (_outputDirty) {
      return writeOutputs(data);
    }
    return Status::Ok();
  }

  return writeOutputs(data);
}

Status PCA9555::toggleOutputBits(uint16_t mask) {
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }

  if (mask == 0) {
    if (_outputDirty) {
      return writeOutputs(PortData::fromCombined(
          static_cast<uint16_t>((static_cast<uint16_t>(_cachedOutput1) << 8) |
                                _cachedOutput0)));
    }
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

  const uint8_t mask = bitMaskForPin(pin);
  const bool isPort0 = (pin < cmd::PINS_PER_PORT);
  const uint8_t& cached = isPort0 ? _cachedOutput0 : _cachedOutput1;
  const uint8_t newVal = cached ^ mask;

  const Port port = isPort0 ? Port::PORT_0 : Port::PORT_1;
  return writeOutput(port, newVal);
}

Status PCA9555::configureInputBits(uint16_t mask) {
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }

  PortData data;
  data.port0 = _cachedConfig0 | static_cast<uint8_t>(mask & 0xFF);
  data.port1 = _cachedConfig1 | static_cast<uint8_t>((mask >> 8) & 0xFF);

  if (data.port0 == _cachedConfig0 && data.port1 == _cachedConfig1) {
    if (_configDirty) {
      return setConfiguration(data);
    }
    return Status::Ok();
  }

  return setConfiguration(data);
}

Status PCA9555::configureOutputBits(uint16_t mask) {
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }

  PortData data;
  data.port0 = _cachedConfig0 & static_cast<uint8_t>(~mask & 0xFF);
  data.port1 = _cachedConfig1 & static_cast<uint8_t>(~(mask >> 8) & 0xFF);

  if (data.port0 == _cachedConfig0 && data.port1 == _cachedConfig1) {
    if (_configDirty) {
      return setConfiguration(data);
    }
    return Status::Ok();
  }

  return setConfiguration(data);
}

Status PCA9555::configureOutputs(uint16_t mask, uint16_t value) {
  Status st = startConfigureOutputsJob(mask, value);
  if (!st.inProgress()) {
    return st;
  }

  return pollJob(_nowMs(), 2);
}

Status PCA9555::setInvertBits(uint16_t mask) {
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }

  PortData data;
  data.port0 = _config.polarityPort0 | static_cast<uint8_t>(mask & 0xFF);
  data.port1 = _config.polarityPort1 | static_cast<uint8_t>((mask >> 8) & 0xFF);

  if (data.port0 == _config.polarityPort0 &&
      data.port1 == _config.polarityPort1) {
    if (_polarityDirty) {
      return setPolarity(data);
    }
    return Status::Ok();
  }

  return setPolarity(data);
}

Status PCA9555::clearInvertBits(uint16_t mask) {
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }

  PortData data;
  data.port0 = _config.polarityPort0 & static_cast<uint8_t>(~mask & 0xFF);
  data.port1 = _config.polarityPort1 & static_cast<uint8_t>(~(mask >> 8) & 0xFF);

  if (data.port0 == _config.polarityPort0 &&
      data.port1 == _config.polarityPort1) {
    if (_polarityDirty) {
      return setPolarity(data);
    }
    return Status::Ok();
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
  _clearDirtyForRegisterPair(cmd::REG_CONFIG_PORT_0, 2);

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
  _clearDirtyForRegisterPair(cmd::REG_POLARITY_INV_0, 2);
  return Status::Ok();
}

Status PCA9555::setPinPolarity(Pin pin, bool inverted) {
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
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
    if (_polarityDirty) {
      PortData data;
      data.port0 = _config.polarityPort0;
      data.port1 = _config.polarityPort1;
      return setPolarity(data);
    }
    return Status::Ok();
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
  Status ready = _requireReadyForPublicIo();
  if (!ready.ok()) {
    return ready;
  }
  if (!isValidPin(pin)) {
    return Status::Error(Err::INVALID_PARAM, "Pin number out of range (0-15)");
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
    if (_configDirty) {
      PortData data;
      data.port0 = _cachedConfig0;
      data.port1 = _cachedConfig1;
      return setConfiguration(data);
    }
    return Status::Ok();
  }

  const Port port = isPort0 ? Port::PORT_0 : Port::PORT_1;
  return setPortConfiguration(port, newVal);
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

  Status st = readRegs(startReg, buf, len);
  if (!st.ok()) {
    return st;
  }

  for (size_t i = 0; i < len; ++i) {
    _syncShadowRegister(static_cast<uint8_t>(startReg + static_cast<uint8_t>(i)),
                       buf[i]);
  }
  _clearDirtyForRegisterPair(startReg, len);

  if (isInputRegister(startReg) && _config.applyInterruptErrata) {
    st = _applyInterruptErrata();
    if (!st.ok()) {
      return st;
    }
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
    _syncShadowRegister(static_cast<uint8_t>(startReg + static_cast<uint8_t>(i)),
                       buf[i]);
  }
  return Status::Ok();
}

// ===========================================================================
// Transport Wrappers
// ===========================================================================

Status PCA9555::_i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                                 uint8_t* rxBuf, size_t rxLen) {
  if (txBuf == nullptr || txLen == 0 || (rxLen > 0 && rxBuf == nullptr)) {
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
  if (txBuf == nullptr || txLen == 0 || (rxLen > 0 && rxBuf == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status blocked = _offlineBlockedStatus();
  if (!blocked.ok()) {
    return blocked;
  }

  Status st = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status PCA9555::_i2cWriteTracked(const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status blocked = _offlineBlockedStatus();
  if (!blocked.ok()) {
    return blocked;
  }

  Status st = _i2cWriteRaw(buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

// ===========================================================================
// Register Access (Internal)
// ===========================================================================

Status PCA9555::readRegs(uint8_t startReg, uint8_t* buf, size_t len) {
  if (_jobType != JobType::NONE && !_jobInstructionActive) {
    return Status::Error(Err::BUSY, "Chunked job active");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (!isValidRegister(startReg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address out of range");
  }
  const size_t pairRemaining = 2U - static_cast<size_t>(startReg & 0x01U);
  if (len > pairRemaining) {
    return Status::Error(Err::INVALID_PARAM, "Read crosses register pair boundary");
  }

  uint8_t reg = startReg;
  return _i2cWriteReadTracked(&reg, 1, buf, len);
}

Status PCA9555::writeRegs(uint8_t startReg, const uint8_t* buf, size_t len) {
  if (_jobType != JobType::NONE && !_jobInstructionActive) {
    return Status::Error(Err::BUSY, "Chunked job active");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid write buffer");
  }
  if (!isValidRegister(startReg)) {
    return Status::Error(Err::INVALID_PARAM, "Register address out of range");
  }
  if (len > MAX_BULK_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Write length too large");
  }
  const size_t pairRemaining = 2U - static_cast<size_t>(startReg & 0x01U);
  if (len > pairRemaining) {
    return Status::Error(Err::INVALID_PARAM, "Write crosses register pair boundary");
  }

  uint8_t payload[MAX_BULK_LEN + 1] = {};
  payload[0] = startReg;
  std::memcpy(&payload[1], buf, len);

  Status st = _i2cWriteTracked(payload, len + 1);
  if (!st.ok()) {
    if (st.code != Err::OFFLINE) {
      _markDirtyForRegister(startReg);
    }
    return st;
  }

  _clearDirtyForRegisterPair(startReg, len);
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

Status PCA9555::_requireReadyForPublicIo() const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_jobType != JobType::NONE && !_jobInstructionActive) {
    return Status::Error(Err::BUSY, "Chunked job active");
  }
  return _offlineBlockedStatus();
}

Status PCA9555::_offlineBlockedStatus() const {
  if (_initialized && _driverState == DriverState::OFFLINE && !_allowOfflineIo) {
    return Status::Error(Err::OFFLINE, "Driver offline; call recover()");
  }
  return Status::Ok();
}

// ===========================================================================
// Internal Helpers
// ===========================================================================

Status PCA9555::_applyConfig() {
  // Step 1: Set output values BEFORE configuring direction to avoid glitches.
  const uint8_t outBuf[2] = {_config.outputPort0, _config.outputPort1};
  Status st = writeRegs(cmd::REG_OUTPUT_PORT_0, outBuf, 2);
  if (!st.ok()) {
    if (st.code != Err::OFFLINE) {
      _polarityDirty = true;
      _configDirty = true;
    }
    return st;
  }

  // Step 2: Set polarity inversion.
  const uint8_t polBuf[2] = {_config.polarityPort0, _config.polarityPort1};
  st = writeRegs(cmd::REG_POLARITY_INV_0, polBuf, 2);
  if (!st.ok()) {
    if (st.code != Err::OFFLINE) {
      _configDirty = true;
    }
    return st;
  }

  // Step 3: Set pin directions.
  const uint8_t cfgBuf[2] = {_config.configPort0, _config.configPort1};
  st = writeRegs(cmd::REG_CONFIG_PORT_0, cfgBuf, 2);
  if (!st.ok()) {
    return st;
  }

  // Step 4: Read input ports to clear any pending interrupts.
  uint8_t inputBuf[2] = {};
  st = readRegs(cmd::REG_INPUT_PORT_0, inputBuf, 2);
  if (!st.ok()) {
    return st;
  }

  // Step 5: Apply interrupt errata workaround.
  if (_config.applyInterruptErrata) {
    st = _applyInterruptErrata();
    if (!st.ok()) {
      return st;
    }
  }

  // Update cached state
  _cachedOutput0 = _config.outputPort0;
  _cachedOutput1 = _config.outputPort1;
  _cachedConfig0 = _config.configPort0;
  _cachedConfig1 = _config.configPort1;

  return Status::Ok();
}

Status PCA9555::_applyInterruptErrata() {
  // Write a command byte != 0x00 to prevent false interrupt de-assertion
  // when another device on the bus is read.
  // We send just the command byte (register pointer) without data.
  const uint8_t safeCmd = cmd::ERRATA_SAFE_CMD;
  return _i2cWriteTracked(&safeCmd, 1);
}

Status PCA9555::_lockI2c() {
  if (_config.i2cLock == nullptr) {
    return Status::Ok();
  }
  return _config.i2cLock(_config.i2cLockUser);
}

void PCA9555::_unlockI2c() {
  if (_config.i2cUnlock != nullptr) {
    _config.i2cUnlock(_config.i2cLockUser);
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
      if (_config.applyInterruptErrata) {
        _jobStep = JobStep::POINTER_PARK;
      } else {
        _finishJob(Status::Ok());
      }
      return Status::Ok();
    }

    case JobStep::POINTER_PARK: {
      Status st = _applyInterruptErrata();
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
      _outputDirty = false;
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
      _outputDirty = false;
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
      _configDirty = false;
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

void PCA9555::_markDirtyForRegister(uint8_t reg) {
  switch (reg & 0xFEU) {
    case cmd::REG_OUTPUT_PORT_0:
      _outputDirty = true;
      break;
    case cmd::REG_POLARITY_INV_0:
      _polarityDirty = true;
      break;
    case cmd::REG_CONFIG_PORT_0:
      _configDirty = true;
      break;
    default:
      break;
  }
}

void PCA9555::_clearDirtyForRegisterPair(uint8_t startReg, size_t len) {
  if ((startReg & 0x01U) != 0 || len < 2) {
    return;
  }

  switch (startReg) {
    case cmd::REG_OUTPUT_PORT_0:
      _outputDirty = false;
      break;
    case cmd::REG_POLARITY_INV_0:
      _polarityDirty = false;
      break;
    case cmd::REG_CONFIG_PORT_0:
      _configDirty = false;
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
  return millis();
}

} // namespace PCA9555

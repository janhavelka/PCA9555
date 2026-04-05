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

  _cachedOutput0 = config.outputPort0;
  _cachedOutput1 = config.outputPort1;
  _cachedConfig0 = config.configPort0;
  _cachedConfig1 = config.configPort1;

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
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
  (void)nowMs;
  // Currently a no-op. Reserved for future periodic polling support.
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
  _cachedOutput0 = 0xFF;
  _cachedOutput1 = 0xFF;
  _cachedConfig0 = 0xFF;
  _cachedConfig1 = 0xFF;
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
  return snapshot;
}

// ===========================================================================
// Diagnostics
// ===========================================================================

Status PCA9555::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
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

  // Use tracked read to update health counters
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
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
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

  return Status::Ok();
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
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
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

  return _i2cWriteTracked(payload, len + 1);
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
  if (_config.nowMs != nullptr) {
    return _config.nowMs(_config.timeUser);
  }
  return millis();
}

} // namespace PCA9555

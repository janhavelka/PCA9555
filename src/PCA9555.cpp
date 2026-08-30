#include "PCA9555/PCA9555.h"

#include <climits>

namespace PCA9555 {
namespace {

constexpr size_t MAX_BULK_LEN = 2U;

bool validPort(Port port) {
  return port == Port::PORT_0 || port == Port::PORT_1;
}

bool validLevel(Level level) {
  return level == Level::LOW_LEVEL || level == Level::HIGH_LEVEL;
}

bool validDirection(Direction direction) {
  return direction == Direction::INPUT_MODE ||
         direction == Direction::OUTPUT_MODE;
}

bool validRegister(uint8_t reg) { return reg <= cmd::REG_CONFIG_PORT_1; }

bool inputRegister(uint8_t reg) {
  return reg == cmd::REG_INPUT_PORT_0 || reg == cmd::REG_INPUT_PORT_1;
}

uint8_t pairForRegister(uint8_t reg) {
  switch (static_cast<uint8_t>(reg & 0xFEU)) {
    case cmd::REG_INPUT_PORT_0: return PAIR_INPUTS;
    case cmd::REG_OUTPUT_PORT_0: return PAIR_OUTPUTS;
    case cmd::REG_POLARITY_INV_0: return PAIR_POLARITY;
    case cmd::REG_CONFIG_PORT_0: return PAIR_DIRECTIONS;
    default: return PAIR_NONE;
  }
}

constexpr uint8_t portRegister(uint8_t baseReg, Port port) {
  return static_cast<uint8_t>(baseReg + static_cast<uint8_t>(port));
}

constexpr uint16_t withPort(uint16_t combined, Port port, uint8_t value) {
  return port == Port::PORT_0
             ? static_cast<uint16_t>((combined & 0xFF00U) | value)
             : static_cast<uint16_t>((combined & 0x00FFU) |
                                     (static_cast<uint16_t>(value) << 8U));
}

constexpr uint8_t portValue(uint16_t combined, Port port) {
  return static_cast<uint8_t>(combined >>
                              (8U * static_cast<uint8_t>(port)));
}

Status busy(BusyDetail detail, const char* message) {
  return Status::Error(Err::BUSY, message, static_cast<int32_t>(detail));
}

}  // namespace

Status PCA9555::_validateBinding(const Config& config) {
  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "transport callbacks required");
  }
  if (!isValidAddress(config.i2cAddress)) {
    return Status::Error(Err::INVALID_CONFIG, "address must be 0x20..0x27");
  }
  if (config.i2cTimeoutMs < MIN_I2C_TIMEOUT_MS ||
      config.i2cTimeoutMs > MAX_I2C_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_CONFIG, "invalid transfer timeout");
  }
  return Status::Ok();
}

Status PCA9555::bind(const Config& config) {
  const Status valid = _validateBinding(config);
  if (!valid.ok()) return valid;
  if (_operation.active) return busy(BusyDetail::OPERATION_ACTIVE, "operation active");
  if (_operation.resultPending) return busy(BusyDetail::RESULT_PENDING, "result pending");

  _config = config;
  _bound = true;
  _driverState = DriverState::READY;
  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _shadow = RegisterImage{};
  _shadowValidPairs = PAIR_NONE;
  _uncertainPairs = PAIR_NONE;
  _observed = ObservedState{};
  _lastWriteEffect = WriteEffect::NOT_APPLICABLE;
  return Status::Ok();
}

Status PCA9555::detach() {
  if (_operation.active) {
    if (_operation.phase == OperationPhase::POINTER_PARK &&
        _operation.result.cleanupRequired) {
      return busy(BusyDetail::OPERATION_ACTIVE, "pointer cleanup required");
    }
    _finishOperation(OperationOutcome::CANCELLED,
                     Status::Error(Err::CANCELLED, "operation cancelled by detach"),
                     _operation.phase);
  }
  _config = Config{};
  _bound = false;
  _driverState = DriverState::UNINIT;
  _shadowValidPairs = PAIR_NONE;
  _uncertainPairs = PAIR_NONE;
  _observed = ObservedState{};
  return Status::Ok();
}

Status PCA9555::_boundStatus() const {
  return _bound ? Status::Ok()
                : Status::Error(Err::NOT_INITIALIZED, "driver not bound");
}

Status PCA9555::_shadowStatus(uint8_t pairs) const {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (_operation.active && !_insideOperationTransfer) {
    return busy(BusyDetail::OPERATION_ACTIVE, "cooperative operation owns device");
  }
  if ((_uncertainPairs & pairs) != 0U) {
    return Status::Error(Err::STATE_UNCERTAIN, "register state uncertain",
                         static_cast<int32_t>(_uncertainPairs & pairs));
  }
  if ((_shadowValidPairs & pairs) != pairs) {
    return Status::Error(Err::SHADOW_INVALID, "protocol shadow invalid",
                         static_cast<int32_t>(pairs & ~_shadowValidPairs));
  }
  return Status::Ok();
}

uint32_t PCA9555::_nowMs() const {
  return (_bound && _config.nowMs != nullptr) ? _config.nowMs(_config.timeUser) : 0U;
}

Status PCA9555::_mapTransportResult(const TransportResult& result,
                                    size_t expectedTx, size_t expectedRx,
                                    bool registerWrite,
                                    WriteEffect& effect) {
  effect = expectedTx == 0U ? WriteEffect::NOT_APPLICABLE
                            : WriteEffect::MAY_HAVE_COMMITTED;
  if (registerWrite) {
    if (result.code == TransportCode::OK &&
        result.completedTxBytes == expectedTx &&
        result.completedRxBytes == expectedRx) {
      effect = WriteEffect::COMMITTED;
    } else if (result.writeEffect == WriteEffect::NOT_ATTEMPTED &&
               (result.completedTxBytes == 0U ||
                (expectedTx > 1U && result.completedTxBytes <= 1U))) {
      effect = WriteEffect::NOT_ATTEMPTED;
    } else {
      effect = WriteEffect::MAY_HAVE_COMMITTED;
    }
  } else if (expectedTx != 0U) {
    if (result.completedTxBytes == expectedTx) {
      effect = WriteEffect::COMMITTED;
    } else if (result.writeEffect == WriteEffect::NOT_ATTEMPTED &&
               result.completedTxBytes == 0U &&
               result.completedRxBytes == 0U) {
      effect = WriteEffect::NOT_ATTEMPTED;
    }
  }

  if (result.code == TransportCode::OK) {
    if (result.completedTxBytes == expectedTx &&
        result.completedRxBytes == expectedRx) {
      return Status::Ok();
    }
    if (registerWrite && effect != WriteEffect::NOT_ATTEMPTED) {
      effect = WriteEffect::MAY_HAVE_COMMITTED;
    }
    return Status::Error(Err::I2C_ERROR, "incomplete successful transport",
                         result.detail);
  }

  switch (result.code) {
    case TransportCode::NACK_ADDRESS:
      return Status::Error(Err::I2C_NACK_ADDR, "address not acknowledged", result.detail);
    case TransportCode::NACK_DATA:
      return Status::Error(Err::I2C_NACK_DATA, "data not acknowledged", result.detail);
    case TransportCode::TIMEOUT:
      return Status::Error(Err::I2C_TIMEOUT, "transport timeout", result.detail);
    case TransportCode::BUS_ERROR:
      return Status::Error(Err::I2C_BUS, "I2C bus error", result.detail);
    case TransportCode::IO_ERROR:
      return Status::Error(Err::I2C_ERROR, "I2C transport error", result.detail);
    case TransportCode::OK:
      break;
  }
  return Status::Error(Err::I2C_ERROR, "invalid transport result", result.detail);
}

Status PCA9555::_i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                                 uint8_t* rxBuf, size_t rxLen,
                                 WriteEffect& commandEffect) {
  const Status bound = _boundStatus();
  if (!bound.ok()) {
    commandEffect = WriteEffect::NOT_ATTEMPTED;
    return bound;
  }
  const uint32_t timeout = _operationTimeoutActive ? _callbackTimeoutMs
                                                    : _config.i2cTimeoutMs;
  const TransportResult result = _config.i2cWriteRead(
      _config.i2cAddress, txBuf, txLen, rxBuf, rxLen, timeout, _config.i2cUser);
  return _mapTransportResult(result, txLen, rxLen, false, commandEffect);
}

Status PCA9555::_i2cWriteRaw(const uint8_t* buf, size_t len,
                             WriteEffect& effect) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  const uint32_t timeout = _operationTimeoutActive ? _callbackTimeoutMs
                                                    : _config.i2cTimeoutMs;
  const TransportResult result = _config.i2cWrite(
      _config.i2cAddress, buf, len, timeout, _config.i2cUser);
  return _mapTransportResult(result, len, 0U, true, effect);
}

Status PCA9555::_updateHealth(const Status& status) {
  if (!_bound) return status;
  if (status.ok()) {
    _driverState = DriverState::READY;
    _lastOkMs = _nowMs();
    _consecutiveFailures = 0;
    if (_totalSuccess != UINT32_MAX) ++_totalSuccess;
  } else {
    _driverState = DriverState::DEGRADED;
    _lastErrorMs = _nowMs();
    _lastError = status;
    if (_consecutiveFailures != UINT8_MAX) ++_consecutiveFailures;
    if (_totalFailures != UINT32_MAX) ++_totalFailures;
  }
  return status;
}

Status PCA9555::_i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                                     uint8_t* rxBuf, size_t rxLen,
                                     WriteEffect& commandEffect) {
  return _updateHealth(
      _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen, commandEffect));
}

Status PCA9555::_i2cWriteTracked(const uint8_t* buf, size_t len,
                                 WriteEffect& effect) {
  return _updateHealth(_i2cWriteRaw(buf, len, effect));
}

Status PCA9555::_i2cWriteCleanupTracked(const uint8_t* buf, size_t len,
                                        WriteEffect& effect) {
  const Status status = _i2cWriteRaw(buf, len, effect);
  return status.ok() ? status : _updateHealth(status);
}

void PCA9555::_invalidateShadowPair(uint8_t pair) {
  _shadowValidPairs = static_cast<uint8_t>(_shadowValidPairs & ~pair);
  _uncertainPairs = static_cast<uint8_t>(_uncertainPairs | pair);
  _observed.uncertainPairs = _uncertainPairs;
}

void PCA9555::_establishShadowPair(uint8_t pair, uint16_t value) {
  if (pair == PAIR_OUTPUTS) _shadow.outputs = value;
  if (pair == PAIR_POLARITY) _shadow.polarity = value;
  if (pair == PAIR_DIRECTIONS) _shadow.directions = value;
  _shadowValidPairs = static_cast<uint8_t>(_shadowValidPairs | pair);
  _uncertainPairs = static_cast<uint8_t>(_uncertainPairs & ~pair);
  _observed.uncertainPairs = _uncertainPairs;
  // Re-establishing a pair supersedes any earlier readback contradiction, so a
  // snapshot can never report the same pair as both shadow-valid and mismatched.
  _observed.mismatchPairs = static_cast<uint8_t>(_observed.mismatchPairs & ~pair);
}

uint16_t PCA9555::_shadowValue(uint8_t pair) const {
  if (pair == PAIR_OUTPUTS) return _shadow.outputs;
  if (pair == PAIR_POLARITY) return _shadow.polarity;
  if (pair == PAIR_DIRECTIONS) return _shadow.directions;
  return 0U;
}

void PCA9555::_recordPairObservation(uint8_t pair, uint16_t value,
                                     uint32_t nowMs) {
  if (pair == PAIR_INPUTS) _observed.inputs = value;
  if (pair == PAIR_OUTPUTS) _observed.registers.outputs = value;
  if (pair == PAIR_POLARITY) _observed.registers.polarity = value;
  if (pair == PAIR_DIRECTIONS) _observed.registers.directions = value;
  if (pair != PAIR_INPUTS) {
    if ((_shadowValidPairs & pair) != 0U &&
        value != _shadowValue(pair)) {
      _observed.mismatchPairs = static_cast<uint8_t>(
          _observed.mismatchPairs | pair);
      _shadowValidPairs = static_cast<uint8_t>(_shadowValidPairs & ~pair);
    } else {
      // Either the full pair matches, or no valid protocol comparison basis
      // exists. Do not carry stale mismatch evidence into this observation.
      _observed.mismatchPairs = static_cast<uint8_t>(
          _observed.mismatchPairs & ~pair);
    }
  }
  _observed.validPairs = static_cast<uint8_t>(_observed.validPairs | pair);
  _observed.uncertainPairs = static_cast<uint8_t>(_uncertainPairs);
  _observed.observedAtMs = nowMs;
}

void PCA9555::_syncObservedRegister(uint8_t reg, uint8_t value,
                                    uint32_t nowMs) {
  const uint8_t pair = pairForRegister(reg);
  if (pair == PAIR_NONE) return;
  if (pair != PAIR_INPUTS && (_shadowValidPairs & pair) != 0U) {
    const uint16_t shadow = _shadowValue(pair);
    const uint8_t expected = (reg & 0x01U) == 0U
        ? static_cast<uint8_t>(shadow & 0xFFU)
        : static_cast<uint8_t>((shadow >> 8U) & 0xFFU);
    if (value != expected) {
      _observed.mismatchPairs = static_cast<uint8_t>(
          _observed.mismatchPairs | pair);
      _shadowValidPairs = static_cast<uint8_t>(_shadowValidPairs & ~pair);
    }
  }
  uint16_t combined = 0U;
  if (pair == PAIR_INPUTS) combined = _observed.inputs;
  if (pair == PAIR_OUTPUTS) combined = _observed.registers.outputs;
  if (pair == PAIR_POLARITY) combined = _observed.registers.polarity;
  if (pair == PAIR_DIRECTIONS) combined = _observed.registers.directions;
  if ((reg & 0x01U) == 0U) {
    combined = static_cast<uint16_t>((combined & 0xFF00U) | value);
  } else {
    combined = static_cast<uint16_t>((combined & 0x00FFU) |
                                     (static_cast<uint16_t>(value) << 8U));
  }
  // A single-byte observation is not fresh evidence for the other byte in the
  // pair, so it neither establishes whole-pair validity nor compares a stale
  // companion byte with the protocol shadow.
  if (pair == PAIR_INPUTS) _observed.inputs = combined;
  if (pair == PAIR_OUTPUTS) _observed.registers.outputs = combined;
  if (pair == PAIR_POLARITY) _observed.registers.polarity = combined;
  if (pair == PAIR_DIRECTIONS) _observed.registers.directions = combined;
  _observed.validPairs = static_cast<uint8_t>(_observed.validPairs & ~pair);
  _observed.uncertainPairs = static_cast<uint8_t>(_uncertainPairs);
  _observed.observedAtMs = nowMs;
}

Status PCA9555::_readRegs(uint8_t startReg, uint8_t* buf, size_t len,
                          bool tracked, WriteEffect* commandEffect) {
  if (commandEffect != nullptr) {
    *commandEffect = WriteEffect::NOT_ATTEMPTED;
  }
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (_operation.active && !_insideOperationTransfer) {
    return busy(BusyDetail::OPERATION_ACTIVE, "cooperative operation owns device");
  }
  if (!validRegister(startReg) || buf == nullptr || len == 0U ||
      len > MAX_BULK_LEN) {
    return Status::Error(Err::INVALID_PARAM, "invalid register read");
  }
  const uint8_t command = startReg;
  WriteEffect localEffect = WriteEffect::NOT_ATTEMPTED;
  const Status status = tracked
      ? _i2cWriteReadTracked(&command, 1U, buf, len, localEffect)
      : _i2cWriteReadRaw(&command, 1U, buf, len, localEffect);
  // Only a callback attempt can invalidate retained full-pair evidence. Local
  // preflight failures returned above (for example BUSY) leave the timestamped
  // historical observation intact.
  if (!status.ok() && len == 2U) {
    _observed.validPairs = static_cast<uint8_t>(
        _observed.validPairs & ~pairForRegister(startReg));
  }
  if (commandEffect != nullptr) *commandEffect = localEffect;
  return status;
}

Status PCA9555::_writeRegs(uint8_t startReg, const uint8_t* buf, size_t len,
                           uint8_t affectedPair, uint16_t intended,
                           bool establishWholePair, bool tracked) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (_operation.active && !_insideOperationTransfer) {
    return busy(BusyDetail::OPERATION_ACTIVE, "cooperative operation owns device");
  }
  if (!validRegister(startReg) || inputRegister(startReg) || buf == nullptr ||
      len == 0U || len > MAX_BULK_LEN) {
    return Status::Error(Err::INVALID_PARAM, "invalid register write");
  }
  uint8_t tx[3] = {startReg, 0U, 0U};
  for (size_t i = 0; i < len; ++i) tx[i + 1U] = buf[i];
  WriteEffect effect = WriteEffect::NOT_APPLICABLE;
  const Status status = tracked ? _i2cWriteTracked(tx, len + 1U, effect)
                                : _i2cWriteRaw(tx, len + 1U, effect);
  _lastWriteEffect = effect;
  if (effect == WriteEffect::MAY_HAVE_COMMITTED) {
    _invalidateShadowPair(affectedPair);
  } else if (status.ok() && effect == WriteEffect::COMMITTED &&
             establishWholePair) {
    _establishShadowPair(affectedPair, intended);
  }
  return status;
}

Status PCA9555::_readPair(uint8_t startReg, uint16_t& value, uint32_t nowMs) {
  uint8_t data[2] = {0U, 0U};
  const Status status = _readRegs(startReg, data, 2U, true);
  if (!status.ok()) return status;
  value = static_cast<uint16_t>(data[0] |
                                (static_cast<uint16_t>(data[1]) << 8U));
  _recordPairObservation(pairForRegister(startReg), value, nowMs);
  return Status::Ok();
}

Status PCA9555::_writePair(uint8_t startReg, uint16_t value, bool tracked) {
  const uint8_t data[2] = {static_cast<uint8_t>(value & 0xFFU),
                           static_cast<uint8_t>((value >> 8U) & 0xFFU)};
  return _writeRegs(startReg, data, 2U, pairForRegister(startReg), value, true,
                    tracked);
}

Status PCA9555::_writePort(uint8_t reg, uint8_t value, uint8_t pair,
                           uint16_t intendedCombined) {
  const bool pairWasValid = (_shadowValidPairs & pair) != 0U &&
                            (_uncertainPairs & pair) == 0U;
  const Status status = _writeRegs(reg, &value, 1U, pair, intendedCombined,
                                   false, true);
  if (status.ok() && pairWasValid) _establishShadowPair(pair, intendedCombined);
  return status;
}

Status PCA9555::_readWritablePortRegister(uint8_t baseReg, Port port,
                                           uint8_t& value) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (!validPort(port)) return Status::Error(Err::INVALID_PARAM, "invalid port");
  const uint8_t reg = portRegister(baseReg, port);
  uint8_t observed = 0U;
  const Status status = _readRegs(reg, &observed, 1U, true);
  if (status.ok()) {
    value = observed;
    _syncObservedRegister(reg, observed, _nowMs());
  }
  return status;
}

Status PCA9555::_parkPointer() {
  const uint8_t command = cmd::ERRATA_SAFE_CMD;
  WriteEffect effect = WriteEffect::NOT_APPLICABLE;
  // The park is protocol cleanup, not caller-requested work. A failed park is
  // real transport evidence, but a successful one must not be counted as a
  // success: doing so would clear the DEGRADED state and the consecutive-failure
  // count set by the input read this park is cleaning up after.
  return _i2cWriteCleanupTracked(&command, 1U, effect);
}

Status PCA9555::_readInputPair(PortData& data, bool& readCompleted) {
  uint8_t values[2] = {0U, 0U};
  const Status status = _readInputRegisters(
      cmd::REG_INPUT_PORT_0, values, 2U, readCompleted);
  if (readCompleted) {
    const uint16_t value = static_cast<uint16_t>(
        values[0] | (static_cast<uint16_t>(values[1]) << 8U));
    data = PortData::fromCombined(value);
    _recordPairObservation(PAIR_INPUTS, value, _nowMs());
  }
  return status;
}

Status PCA9555::_readInputRegisters(uint8_t startReg, uint8_t* buf,
                                    size_t len, bool& readCompleted) {
  readCompleted = false;
  WriteEffect commandEffect = WriteEffect::NOT_ATTEMPTED;
  const Status readStatus =
      _readRegs(startReg, buf, len, true, &commandEffect);
  readCompleted = readStatus.ok();
  if (commandEffect == WriteEffect::NOT_ATTEMPTED) return readStatus;

  const Status cleanupStatus = _parkPointer();
  return readStatus.ok() ? cleanupStatus : readStatus;
}

Status PCA9555::probe() {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (_operation.active) {
    return busy(BusyDetail::OPERATION_ACTIVE, "cooperative operation owns device");
  }
  const uint8_t reg = cmd::REG_CONFIG_PORT_0;
  WriteEffect effect = WriteEffect::NOT_ATTEMPTED;
  Status status = _i2cWriteRaw(&reg, 1U, effect);
  if (status.code == Err::I2C_NACK_ADDR) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "address did not respond",
                         status.detail);
  }
  return status;
}

Status PCA9555::checkPorDefaults(PortData& observedDirections) {
  uint16_t value = 0U;
  const Status status = _readPair(cmd::REG_CONFIG_PORT_0, value, _nowMs());
  if (!status.ok()) return status;
  observedDirections = PortData::fromCombined(value);
  if (value != 0xFFFFU) {
    return Status::Error(Err::CONFIG_REG_MISMATCH,
                         "configuration registers are not POR defaults",
                         static_cast<int32_t>(value));
  }
  return Status::Ok();
}

Status PCA9555::readObservedState(ObservedState& observed) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (_operation.active) {
    return busy(BusyDetail::OPERATION_ACTIVE, "cooperative operation owns device");
  }
  ObservedState current{};
  static constexpr uint8_t REGS[3] = {
      cmd::REG_OUTPUT_PORT_0, cmd::REG_POLARITY_INV_0,
      cmd::REG_CONFIG_PORT_0};
  for (uint8_t i = 0U; i < 3U; ++i) {
    uint16_t value = 0U;
    const uint8_t pair = pairForRegister(REGS[i]);
    const Status status = _readPair(REGS[i], value, _nowMs());
    if (!status.ok()) {
      current.uncertainPairs = _uncertainPairs;
      observed = current;
      return status;
    }
    if (pair == PAIR_OUTPUTS) current.registers.outputs = value;
    if (pair == PAIR_POLARITY) current.registers.polarity = value;
    if (pair == PAIR_DIRECTIONS) current.registers.directions = value;
    current.validPairs = static_cast<uint8_t>(current.validPairs | pair);
    current.observedAtMs = _observed.observedAtMs;
    if ((_observed.mismatchPairs & pair) != 0U) {
      current.mismatchPairs = static_cast<uint8_t>(
          current.mismatchPairs | pair);
    }
  }
  current.uncertainPairs = _uncertainPairs;
  observed = current;
  return Status::Ok();
}

bool PCA9555::_deadlineReached(uint32_t nowMs) const {
  return static_cast<int32_t>(nowMs - _operation.deadlineMs) >= 0;
}

Status PCA9555::_startOperation(OperationKind kind, uint32_t requestId,
                                const RegisterImage& expected, uint32_t nowMs,
                                uint32_t timeoutMs,
                                OperationPhase firstPhase) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (requestId == 0U || timeoutMs == 0U || timeoutMs > INT32_MAX) {
    return Status::Error(Err::INVALID_PARAM,
                         "requestId and operation timeout must be nonzero");
  }
  if (_operation.active) return busy(BusyDetail::OPERATION_ACTIVE, "operation active");
  if (_operation.resultPending) return busy(BusyDetail::RESULT_PENDING, "result pending");

  _operation = OperationSlot{};
  _operation.active = true;
  _operation.requestId = requestId;
  _operation.deadlineMs = nowMs + timeoutMs;
  _operation.kind = kind;
  _operation.phase = firstPhase;
  _operation.expected = expected;
  _operation.result.requestId = requestId;
  _operation.result.kind = kind;
  _operation.result.outcome = OperationOutcome::ACTIVE;
  _operation.result.observed = ObservedState{};
  return Status::Error(Err::IN_PROGRESS, "operation admitted");
}

Status PCA9555::startApplyImage(uint32_t requestId,
                                const RegisterImage& image, uint32_t nowMs,
                                uint32_t timeoutMs) {
  return _startOperation(OperationKind::APPLY_IMAGE, requestId, image, nowMs,
                         timeoutMs, OperationPhase::APPLY_OUTPUTS);
}

Status PCA9555::startVerifyImage(uint32_t requestId,
                                 const RegisterImage& expected,
                                 uint32_t nowMs, uint32_t timeoutMs) {
  return _startOperation(OperationKind::VERIFY_IMAGE, requestId, expected, nowMs,
                         timeoutMs, OperationPhase::VERIFY_OUTPUTS);
}

Status PCA9555::startReadInputs(uint32_t requestId, uint32_t nowMs,
                                uint32_t timeoutMs) {
  return _startOperation(OperationKind::READ_INPUTS, requestId, RegisterImage{},
                         nowMs, timeoutMs, OperationPhase::READ_INPUTS);
}

void PCA9555::_compareObserved(uint8_t pair, uint16_t expected) {
  uint16_t actual = 0U;
  if (pair == PAIR_OUTPUTS) actual = _observed.registers.outputs;
  if (pair == PAIR_POLARITY) actual = _observed.registers.polarity;
  if (pair == PAIR_DIRECTIONS) actual = _observed.registers.directions;
  if (actual != expected) {
    _operation.result.mismatchPairs =
        static_cast<uint8_t>(_operation.result.mismatchPairs | pair);
  } else {
    // Apply and explicit verify both carry a complete caller-owned image.
    // Matching readback is therefore sufficient to reconcile protocol state.
    _establishShadowPair(pair, expected);
    _observed.mismatchPairs = static_cast<uint8_t>(
        _observed.mismatchPairs & ~pair);
  }
}

void PCA9555::_finishOperation(OperationOutcome outcome, const Status& status,
                               OperationPhase terminalPhase) {
  _operation.result.outcome = outcome;
  _operation.result.status = status;
  _operation.result.terminalPhase = terminalPhase;
  _operation.result.shadowValidPairs = _shadowValidPairs;
  _operation.result.uncertainPairs = _uncertainPairs;
  _operation.result.observed.mismatchPairs = _operation.result.mismatchPairs;
  _operation.result.observed.uncertainPairs = _uncertainPairs;
  _operation.active = false;
  _operation.resultPending = true;
  _operation.phase = OperationPhase::COMPLETE;
  _operationTimeoutActive = false;
  _callbackTimeoutMs = 0U;
}

Status PCA9555::_requestTerminal(uint32_t requestId,
                                 OperationOutcome outcome,
                                 const Status& status) {
  if (!_operation.active) {
    if (_operation.resultPending) {
      return busy(BusyDetail::RESULT_PENDING, "result pending");
    }
    return Status::Error(Err::NO_RESULT, "no active operation");
  }
  if (requestId != _operation.requestId) {
    return busy(BusyDetail::REQUEST_ID_MISMATCH, "request ID mismatch");
  }
  if (_operation.phase == OperationPhase::POINTER_PARK &&
      _operation.result.cleanupRequired) {
    if (!_operation.terminalRequested) {
      _operation.terminalRequested = true;
      _operation.requestedOutcome = outcome;
      _operation.requestedTerminalPhase = _operation.phase;
      _operation.requestedStatus = status;
    }
    return Status::Error(Err::IN_PROGRESS, "pointer cleanup still required");
  }
  _finishOperation(outcome, status, _operation.phase);
  return Status::Ok();
}

Status PCA9555::cancelOperation(uint32_t requestId) {
  return _requestTerminal(requestId, OperationOutcome::CANCELLED,
                          Status::Error(Err::CANCELLED, "operation cancelled"));
}

Status PCA9555::timeoutOperation(uint32_t requestId) {
  return _requestTerminal(requestId, OperationOutcome::TIMED_OUT,
                          Status::Error(Err::TIMEOUT, "operation timed out"));
}

Status PCA9555::_executeOperationTransfer(uint32_t nowMs) {
  Status status = Status::Ok();
  uint16_t value = 0U;
  const OperationPhase phase = _operation.phase;
  bool deferredReadFailure = false;

  switch (phase) {
    case OperationPhase::APPLY_OUTPUTS:
      status = _writePair(cmd::REG_OUTPUT_PORT_0, _operation.expected.outputs);
      _operation.result.lastWriteEffect = _lastWriteEffect;
      if (status.ok()) {
        _operation.result.completedPairs = static_cast<uint8_t>(
            _operation.result.completedPairs | PAIR_OUTPUTS);
        _operation.phase = OperationPhase::APPLY_POLARITY;
      }
      break;

    case OperationPhase::APPLY_POLARITY:
      status = _writePair(cmd::REG_POLARITY_INV_0,
                          _operation.expected.polarity);
      _operation.result.lastWriteEffect = _lastWriteEffect;
      if (status.ok()) {
        _operation.result.completedPairs = static_cast<uint8_t>(
            _operation.result.completedPairs | PAIR_POLARITY);
        _operation.phase = OperationPhase::APPLY_DIRECTIONS;
      }
      break;

    case OperationPhase::APPLY_DIRECTIONS:
      status = _writePair(cmd::REG_CONFIG_PORT_0,
                          _operation.expected.directions);
      _operation.result.lastWriteEffect = _lastWriteEffect;
      if (status.ok()) {
        _operation.result.completedPairs = static_cast<uint8_t>(
            _operation.result.completedPairs | PAIR_DIRECTIONS);
        _operation.phase = OperationPhase::VERIFY_OUTPUTS;
      }
      break;

    case OperationPhase::VERIFY_OUTPUTS:
      status = _readPair(cmd::REG_OUTPUT_PORT_0, value, nowMs);
      if (status.ok()) {
        _operation.result.observed.registers.outputs = value;
        _operation.result.observed.validPairs = static_cast<uint8_t>(
            _operation.result.observed.validPairs | PAIR_OUTPUTS);
        _operation.result.completedPairs = static_cast<uint8_t>(
            _operation.result.completedPairs | PAIR_OUTPUTS);
        _operation.result.observed.observedAtMs = nowMs;
        _compareObserved(PAIR_OUTPUTS, _operation.expected.outputs);
        _operation.phase = OperationPhase::VERIFY_POLARITY;
      }
      break;

    case OperationPhase::VERIFY_POLARITY:
      status = _readPair(cmd::REG_POLARITY_INV_0, value, nowMs);
      if (status.ok()) {
        _operation.result.observed.registers.polarity = value;
        _operation.result.observed.validPairs = static_cast<uint8_t>(
            _operation.result.observed.validPairs | PAIR_POLARITY);
        _operation.result.completedPairs = static_cast<uint8_t>(
            _operation.result.completedPairs | PAIR_POLARITY);
        _operation.result.observed.observedAtMs = nowMs;
        _compareObserved(PAIR_POLARITY, _operation.expected.polarity);
        _operation.phase = OperationPhase::VERIFY_DIRECTIONS;
      }
      break;

    case OperationPhase::VERIFY_DIRECTIONS:
      status = _readPair(cmd::REG_CONFIG_PORT_0, value, nowMs);
      if (status.ok()) {
        _operation.result.observed.registers.directions = value;
        _operation.result.observed.validPairs = static_cast<uint8_t>(
            _operation.result.observed.validPairs | PAIR_DIRECTIONS);
        _operation.result.completedPairs = static_cast<uint8_t>(
            _operation.result.completedPairs | PAIR_DIRECTIONS);
        _operation.result.observed.observedAtMs = nowMs;
        _compareObserved(PAIR_DIRECTIONS, _operation.expected.directions);
        if (_operation.result.mismatchPairs != PAIR_NONE) {
          _finishOperation(
              OperationOutcome::MISMATCH,
              Status::Error(Err::VERIFY_MISMATCH, "register image mismatch",
                            _operation.result.mismatchPairs), phase);
        } else if (_operation.kind == OperationKind::APPLY_IMAGE) {
          _operation.phase = OperationPhase::READ_INPUTS;
        } else {
          _finishOperation(OperationOutcome::SUCCEEDED, Status::Ok(), phase);
        }
      }
      break;

    case OperationPhase::READ_INPUTS: {
      uint8_t data[2] = {0U, 0U};
      WriteEffect commandEffect = WriteEffect::NOT_ATTEMPTED;
      status = _readRegs(cmd::REG_INPUT_PORT_0, data, 2U, true,
                         &commandEffect);
      if (status.ok()) {
        value = static_cast<uint16_t>(data[0] |
                                      (static_cast<uint16_t>(data[1]) << 8U));
        _recordPairObservation(PAIR_INPUTS, value, nowMs);
        _operation.result.observed.inputs = value;
        _operation.result.observed.validPairs = static_cast<uint8_t>(
            _operation.result.observed.validPairs | PAIR_INPUTS);
        _operation.result.observed.observedAtMs = nowMs;
        _operation.result.completedPairs = static_cast<uint8_t>(
            _operation.result.completedPairs | PAIR_INPUTS);
        _operation.result.cleanupRequired = true;
        _operation.phase = OperationPhase::POINTER_PARK;
      }
      if (!status.ok() && commandEffect != WriteEffect::NOT_ATTEMPTED) {
        // The input command may be the chip's active register pointer even
        // though the receive phase failed. Preserve the read failure and park
        // the pointer as a separate, bounded cleanup transfer.
        _operation.result.cleanupRequired = true;
        _operation.terminalRequested = true;
        _operation.requestedOutcome = OperationOutcome::FAILED;
        _operation.requestedTerminalPhase = phase;
        _operation.requestedStatus = status;
        _operation.phase = OperationPhase::POINTER_PARK;
        deferredReadFailure = true;
      }
      break;
    }

    case OperationPhase::POINTER_PARK:
      _operation.result.cleanupAttempted = true;
      status = _parkPointer();
      _operation.result.cleanupStatus = status;
      if (status.ok()) _operation.result.cleanupRequired = false;
      if (_operation.terminalRequested) {
        const OperationPhase terminalPhase =
            _operation.requestedTerminalPhase == OperationPhase::NONE
                ? phase
                : _operation.requestedTerminalPhase;
        _finishOperation(_operation.requestedOutcome,
                         _operation.requestedStatus, terminalPhase);
      } else if (status.ok()) {
        _finishOperation(OperationOutcome::SUCCEEDED, Status::Ok(), phase);
      }
      break;

    case OperationPhase::NONE:
    case OperationPhase::COMPLETE:
      return Status::Error(Err::INVALID_PARAM, "invalid operation phase");
  }

  if (!status.ok() && _operation.active && !deferredReadFailure) {
    const bool indeterminate = _lastWriteEffect == WriteEffect::MAY_HAVE_COMMITTED &&
        (phase == OperationPhase::APPLY_OUTPUTS ||
         phase == OperationPhase::APPLY_POLARITY ||
         phase == OperationPhase::APPLY_DIRECTIONS);
    _finishOperation(indeterminate ? OperationOutcome::INDETERMINATE
                                   : OperationOutcome::FAILED,
                     status, phase);
  }
  return status;
}

Status PCA9555::pollOperation(uint32_t requestId, uint32_t nowMs,
                              uint8_t transactionBudget,
                              uint8_t& transactionsUsed) {
  transactionsUsed = 0U;
  if (!_operation.active) {
    if (_operation.resultPending) {
      return busy(BusyDetail::RESULT_PENDING, "result pending");
    }
    return Status::Error(Err::NO_RESULT, "no active operation");
  }
  if (requestId != _operation.requestId) {
    return busy(BusyDetail::REQUEST_ID_MISMATCH, "request ID mismatch");
  }

  const bool deadlineAtEntry = _deadlineReached(nowMs);
  const bool cleanupOwedAtEntry =
      _operation.phase == OperationPhase::POINTER_PARK &&
      _operation.result.cleanupRequired;
  if (deadlineAtEntry && !cleanupOwedAtEntry) {
    _finishOperation(OperationOutcome::TIMED_OUT,
                     Status::Error(Err::TIMEOUT,
                                   "operation deadline expired"),
                     _operation.phase);
    return _operation.result.status;
  }
  if (deadlineAtEntry && cleanupOwedAtEntry) {
    if (!_operation.terminalRequested) {
      _operation.terminalRequested = true;
      _operation.requestedOutcome = OperationOutcome::TIMED_OUT;
      _operation.requestedTerminalPhase = _operation.phase;
      _operation.requestedStatus =
          Status::Error(Err::TIMEOUT, "operation deadline expired");
    }
    _operation.result.cleanupAfterDeadline = true;
  }
  if (transactionBudget == 0U) {
    return Status::Error(Err::IN_PROGRESS, "zero transaction budget");
  }

  uint8_t operationRemaining = MAX_READ_INPUTS_TRANSACTIONS;
  if (_operation.kind == OperationKind::APPLY_IMAGE) {
    operationRemaining = MAX_APPLY_IMAGE_TRANSACTIONS;
  } else if (_operation.kind == OperationKind::VERIFY_IMAGE) {
    operationRemaining = MAX_VERIFY_IMAGE_TRANSACTIONS;
  }
  if (_operation.result.transactionsUsed < operationRemaining) {
    operationRemaining = static_cast<uint8_t>(
        operationRemaining - _operation.result.transactionsUsed);
  } else {
    operationRemaining = 1U;
  }
  uint8_t callLimit = transactionBudget < operationRemaining
                          ? transactionBudget
                          : operationRemaining;
  uint32_t timeoutAllowanceMs = _operation.deadlineMs - nowMs;
  if (static_cast<int32_t>(timeoutAllowanceMs) > 0 &&
      timeoutAllowanceMs < callLimit) {
    // Every callback receives at least 1 ms, so no more callbacks can fit than
    // the number of whole milliseconds remaining in the operation deadline.
    callLimit = static_cast<uint8_t>(timeoutAllowanceMs);
  }
  if (callLimit == 0U) callLimit = 1U;

  while (_operation.active && transactionsUsed < callLimit) {
    if (deadlineAtEntry) {
      _callbackTimeoutMs = _config.i2cTimeoutMs;
    } else {
      const uint8_t callsLeft = static_cast<uint8_t>(callLimit - transactionsUsed);
      uint32_t fairShare = timeoutAllowanceMs / callsLeft;
      if (fairShare == 0U) fairShare = 1U;
      _callbackTimeoutMs = fairShare < _config.i2cTimeoutMs
                               ? fairShare
                               : _config.i2cTimeoutMs;
      if (_callbackTimeoutMs == 0U) _callbackTimeoutMs = 1U;
      if (timeoutAllowanceMs >= _callbackTimeoutMs) {
        timeoutAllowanceMs -= _callbackTimeoutMs;
      } else {
        timeoutAllowanceMs = 0U;
      }
    }
    _operationTimeoutActive = true;
    _insideOperationTransfer = true;
    _executeOperationTransfer(nowMs);
    _insideOperationTransfer = false;
    _operationTimeoutActive = false;
    ++transactionsUsed;
    if (_operation.result.transactionsUsed != UINT8_MAX) {
      ++_operation.result.transactionsUsed;
    }
  }

  if (_operation.active) {
    return Status::Error(Err::IN_PROGRESS, "operation in progress");
  }
  return _operation.result.status;
}

Status PCA9555::takeOperationResult(uint32_t requestId,
                                    OperationResult& result) {
  if (!_operation.resultPending) {
    return Status::Error(Err::NO_RESULT, "no terminal operation result");
  }
  if (requestId != _operation.requestId) {
    return busy(BusyDetail::REQUEST_ID_MISMATCH, "request ID mismatch");
  }
  result = _operation.result;
  _operation = OperationSlot{};
  return Status::Ok();
}

Status PCA9555::readInputs(PortData& data) {
  bool completed = false;
  return _readInputPair(data, completed);
}

Status PCA9555::readInputsAndClearInterrupt(uint16_t& value) {
  PortData data{};
  bool completed = false;
  const Status status = _readInputPair(data, completed);
  if (completed) value = data.combined();
  return status;
}

Status PCA9555::clearInterrupts() {
  PortData ignored{};
  return readInputs(ignored);
}

Status PCA9555::readInput(Port port, uint8_t& value) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (!validPort(port)) return Status::Error(Err::INVALID_PARAM, "invalid port");
  const uint8_t reg = portRegister(cmd::REG_INPUT_PORT_0, port);
  uint8_t observed = 0U;
  bool readCompleted = false;
  const Status status =
      _readInputRegisters(reg, &observed, 1U, readCompleted);
  if (readCompleted) {
    value = observed;
    _syncObservedRegister(reg, observed, _nowMs());
  }
  return status;
}

Status PCA9555::_readPin(Pin pin, Level& level, bool& readCompleted) {
  readCompleted = false;
  if (!isValidPin(pin)) return Status::Error(Err::INVALID_PARAM, "invalid pin");
  uint8_t value = 0U;
  const uint8_t reg = portOf(pin) == Port::PORT_0 ? cmd::REG_INPUT_PORT_0
                                                  : cmd::REG_INPUT_PORT_1;
  const Status status =
      _readInputRegisters(reg, &value, 1U, readCompleted);
  if (readCompleted) {
    _syncObservedRegister(reg, value, _nowMs());
    level = (value & static_cast<uint8_t>(1U << bitOf(pin))) != 0U
                ? Level::HIGH_LEVEL
                : Level::LOW_LEVEL;
  }
  return status;
}

Status PCA9555::readPin(Pin pin, Level& level) {
  bool readCompleted = false;
  return _readPin(pin, level, readCompleted);
}

Status PCA9555::readPin(Pin pin, bool& high) {
  Level level = Level::LOW_LEVEL;
  bool readCompleted = false;
  const Status status = _readPin(pin, level, readCompleted);
  if (readCompleted) high = level == Level::HIGH_LEVEL;
  return status;
}

Status PCA9555::writeOutputs(const PortData& data) {
  return _writePair(cmd::REG_OUTPUT_PORT_0, data.combined());
}

Status PCA9555::writeOutput(Port port, uint8_t value) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (!validPort(port)) return Status::Error(Err::INVALID_PARAM, "invalid port");
  return _writePort(portRegister(cmd::REG_OUTPUT_PORT_0, port), value,
                    PAIR_OUTPUTS, withPort(_shadow.outputs, port, value));
}

Status PCA9555::readOutputs(PortData& data) {
  uint16_t value = 0U;
  const Status status = _readPair(cmd::REG_OUTPUT_PORT_0, value, _nowMs());
  if (status.ok()) data = PortData::fromCombined(value);
  return status;
}

Status PCA9555::readOutput(Port port, uint8_t& value) {
  return _readWritablePortRegister(cmd::REG_OUTPUT_PORT_0, port, value);
}

Status PCA9555::writePin(Pin pin, Level level) {
  if (!isValidPin(pin) || !validLevel(level)) {
    return Status::Error(Err::INVALID_PARAM, "invalid pin or level");
  }
  const Status clean = _shadowStatus(PAIR_OUTPUTS);
  if (!clean.ok()) return clean;
  const PinMask mask = pinMask(pin);
  const uint16_t desired = level == Level::HIGH_LEVEL
                               ? static_cast<uint16_t>(_shadow.outputs | mask)
                               : static_cast<uint16_t>(_shadow.outputs & ~mask);
  return _writePair(cmd::REG_OUTPUT_PORT_0, desired);
}

Status PCA9555::writePin(Pin pin, bool high) {
  return writePin(pin, high ? Level::HIGH_LEVEL : Level::LOW_LEVEL);
}

Status PCA9555::readOutputPin(Pin pin, Level& level) {
  if (!isValidPin(pin)) return Status::Error(Err::INVALID_PARAM, "invalid pin");
  uint8_t value = 0U;
  const Status status = readOutput(portOf(pin), value);
  if (status.ok()) {
    level = (value & static_cast<uint8_t>(1U << bitOf(pin))) != 0U
                ? Level::HIGH_LEVEL
                : Level::LOW_LEVEL;
  }
  return status;
}

Status PCA9555::readOutputPin(Pin pin, bool& high) {
  Level level = Level::LOW_LEVEL;
  const Status status = readOutputPin(pin, level);
  if (status.ok()) high = level == Level::HIGH_LEVEL;
  return status;
}

Status PCA9555::preloadOutput(Pin pin, Level level) {
  if (!isValidPin(pin) || !validLevel(level)) {
    return Status::Error(Err::INVALID_PARAM, "invalid pin or level");
  }
  return preloadOutputs(pinMask(pin),
                        level == Level::HIGH_LEVEL ? pinMask(pin) : 0U);
}

Status PCA9555::preloadOutput(Pin pin, bool high) {
  return preloadOutput(pin, high ? Level::HIGH_LEVEL : Level::LOW_LEVEL);
}

Status PCA9555::preloadOutputs(PinMask mask, PinMask values) {
  const Status ready = _shadowStatus(PAIR_NONE);
  if (!ready.ok()) return ready;
  if (mask == 0U) return Status::Ok();
  uint16_t desired = values;
  if (mask != 0xFFFFU) {
    const Status clean = _shadowStatus(PAIR_OUTPUTS);
    if (!clean.ok()) return clean;
    desired = static_cast<uint16_t>((_shadow.outputs & ~mask) | (values & mask));
  }
  return _writePair(cmd::REG_OUTPUT_PORT_0, desired);
}

Status PCA9555::setOutputBits(PinMask mask) {
  const Status clean = _shadowStatus(mask == 0U ? PAIR_NONE : PAIR_OUTPUTS);
  if (!clean.ok()) return clean;
  if (mask == 0U) return Status::Ok();
  return _writePair(cmd::REG_OUTPUT_PORT_0,
                    static_cast<uint16_t>(_shadow.outputs | mask));
}

Status PCA9555::clearOutputBits(PinMask mask) {
  const Status clean = _shadowStatus(mask == 0U ? PAIR_NONE : PAIR_OUTPUTS);
  if (!clean.ok()) return clean;
  if (mask == 0U) return Status::Ok();
  const uint16_t desired = static_cast<uint16_t>(_shadow.outputs & ~mask);
  return _writePair(cmd::REG_OUTPUT_PORT_0, desired);
}

Status PCA9555::toggleOutputBits(PinMask mask) {
  const Status clean = _shadowStatus(mask == 0U ? PAIR_NONE : PAIR_OUTPUTS);
  if (!clean.ok()) return clean;
  if (mask == 0U) return Status::Ok();
  return _writePair(cmd::REG_OUTPUT_PORT_0,
                    static_cast<uint16_t>(_shadow.outputs ^ mask));
}

Status PCA9555::togglePin(Pin pin) {
  if (!isValidPin(pin)) return Status::Error(Err::INVALID_PARAM, "invalid pin");
  return toggleOutputBits(pinMask(pin));
}

Status PCA9555::setConfiguration(const PortData& data) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  const uint16_t directions = data.combined();
  if (directions != 0xFFFFU) {
    const Status outputs = _shadowStatus(PAIR_OUTPUTS);
    if (!outputs.ok()) return outputs;
    const Status preload = _writePair(cmd::REG_OUTPUT_PORT_0, _shadow.outputs);
    if (!preload.ok()) return preload;
  }
  return _writePair(cmd::REG_CONFIG_PORT_0, directions);
}

Status PCA9555::setPortConfiguration(Port port, uint8_t value) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (!validPort(port)) return Status::Error(Err::INVALID_PARAM, "invalid port");
  if (value != 0xFFU) {
    const Status outputs = _shadowStatus(PAIR_OUTPUTS);
    if (!outputs.ok()) return outputs;
    const uint8_t output = portValue(_shadow.outputs, port);
    const uint8_t outputReg = portRegister(cmd::REG_OUTPUT_PORT_0, port);
    const Status preload = _writePort(outputReg, output, PAIR_OUTPUTS,
                                      _shadow.outputs);
    if (!preload.ok()) return preload;
  }
  return _writePort(portRegister(cmd::REG_CONFIG_PORT_0, port), value,
                    PAIR_DIRECTIONS,
                    withPort(_shadow.directions, port, value));
}

Status PCA9555::getConfiguration(PortData& data) {
  uint16_t value = 0U;
  const Status status = _readPair(cmd::REG_CONFIG_PORT_0, value, _nowMs());
  if (status.ok()) data = PortData::fromCombined(value);
  return status;
}

Status PCA9555::getPortConfiguration(Port port, uint8_t& value) {
  return _readWritablePortRegister(cmd::REG_CONFIG_PORT_0, port, value);
}

Status PCA9555::configureOutputs(PinMask outputMask,
                                 PinMask outputValues) {
  // An empty mask still has to answer the ownership question: PAIR_NONE runs the
  // bound and cooperative-operation guards without demanding any shadow pair.
  if (outputMask == 0U) return _shadowStatus(PAIR_NONE);
  const Status clean = _shadowStatus(PAIR_OUTPUTS | PAIR_DIRECTIONS);
  if (!clean.ok()) return clean;
  const uint16_t outputs = static_cast<uint16_t>(
      (_shadow.outputs & ~outputMask) | (outputValues & outputMask));
  const uint16_t directions = static_cast<uint16_t>(
      _shadow.directions & ~outputMask);
  const Status preload = _writePair(cmd::REG_OUTPUT_PORT_0, outputs);
  if (!preload.ok()) return preload;
  return _writePair(cmd::REG_CONFIG_PORT_0, directions);
}

Status PCA9555::configureInputBits(PinMask mask) {
  const Status clean = _shadowStatus(mask == 0U ? PAIR_NONE : PAIR_DIRECTIONS);
  if (!clean.ok()) return clean;
  if (mask == 0U) return Status::Ok();
  const uint16_t directions = static_cast<uint16_t>(_shadow.directions | mask);
  return _writePair(cmd::REG_CONFIG_PORT_0, directions);
}

Status PCA9555::configureOutputBits(PinMask mask) {
  const Status clean = _shadowStatus(
      mask == 0U ? PAIR_NONE : PAIR_OUTPUTS | PAIR_DIRECTIONS);
  if (!clean.ok()) return clean;
  if (mask == 0U) return Status::Ok();
  return configureOutputs(mask, _shadow.outputs);
}

Status PCA9555::setDirection(Pin pin, Direction direction) {
  if (!isValidPin(pin) || !validDirection(direction)) {
    return Status::Error(Err::INVALID_PARAM, "invalid pin direction");
  }
  return direction == Direction::INPUT_MODE ? configureInputBits(pinMask(pin))
                                            : configureOutputBits(pinMask(pin));
}

Status PCA9555::setPinDirection(Pin pin, bool input) {
  return setDirection(pin, input ? Direction::INPUT_MODE
                                 : Direction::OUTPUT_MODE);
}

Status PCA9555::getPinDirection(Pin pin, Direction& direction) {
  if (!isValidPin(pin)) return Status::Error(Err::INVALID_PARAM, "invalid pin");
  uint8_t value = 0U;
  const Status status = getPortConfiguration(portOf(pin), value);
  if (status.ok()) {
    direction = (value & static_cast<uint8_t>(1U << bitOf(pin))) != 0U
                    ? Direction::INPUT_MODE
                    : Direction::OUTPUT_MODE;
  }
  return status;
}

Status PCA9555::getPinDirection(Pin pin, bool& input) {
  Direction direction = Direction::INPUT_MODE;
  const Status status = getPinDirection(pin, direction);
  if (status.ok()) input = direction == Direction::INPUT_MODE;
  return status;
}

Status PCA9555::setPolarity(const PortData& data) {
  return _writePair(cmd::REG_POLARITY_INV_0, data.combined());
}

Status PCA9555::setPortPolarity(Port port, uint8_t value) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (!validPort(port)) return Status::Error(Err::INVALID_PARAM, "invalid port");
  return _writePort(portRegister(cmd::REG_POLARITY_INV_0, port), value,
                    PAIR_POLARITY, withPort(_shadow.polarity, port, value));
}

Status PCA9555::getPolarity(PortData& data) {
  uint16_t value = 0U;
  const Status status = _readPair(cmd::REG_POLARITY_INV_0, value, _nowMs());
  if (status.ok()) data = PortData::fromCombined(value);
  return status;
}

Status PCA9555::getPortPolarity(Port port, uint8_t& value) {
  return _readWritablePortRegister(cmd::REG_POLARITY_INV_0, port, value);
}

Status PCA9555::setPinPolarity(Pin pin, bool inverted) {
  if (!isValidPin(pin)) return Status::Error(Err::INVALID_PARAM, "invalid pin");
  const Status clean = _shadowStatus(PAIR_POLARITY);
  if (!clean.ok()) return clean;
  const PinMask mask = pinMask(pin);
  const uint16_t desired = inverted
      ? static_cast<uint16_t>(_shadow.polarity | mask)
      : static_cast<uint16_t>(_shadow.polarity & ~mask);
  return _writePair(cmd::REG_POLARITY_INV_0, desired);
}

Status PCA9555::getPinPolarity(Pin pin, bool& inverted) {
  if (!isValidPin(pin)) return Status::Error(Err::INVALID_PARAM, "invalid pin");
  uint8_t value = 0U;
  const Status status = getPortPolarity(portOf(pin), value);
  if (status.ok()) {
    inverted = (value & static_cast<uint8_t>(1U << bitOf(pin))) != 0U;
  }
  return status;
}

Status PCA9555::setInvertBits(PinMask mask) {
  const Status clean = _shadowStatus(mask == 0U ? PAIR_NONE : PAIR_POLARITY);
  if (!clean.ok()) return clean;
  if (mask == 0U) return Status::Ok();
  const uint16_t desired = static_cast<uint16_t>(_shadow.polarity | mask);
  return _writePair(cmd::REG_POLARITY_INV_0, desired);
}

Status PCA9555::clearInvertBits(PinMask mask) {
  const Status clean = _shadowStatus(mask == 0U ? PAIR_NONE : PAIR_POLARITY);
  if (!clean.ok()) return clean;
  if (mask == 0U) return Status::Ok();
  const uint16_t desired = static_cast<uint16_t>(_shadow.polarity & ~mask);
  return _writePair(cmd::REG_POLARITY_INV_0, desired);
}

Status PCA9555::readRegister(uint8_t reg, uint8_t& value) {
  return readRegisters(reg, &value, 1U);
}

Status PCA9555::readRegisters(uint8_t startReg, uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0U || len > MAX_BULK_LEN) {
    return Status::Error(Err::INVALID_PARAM, "invalid register read");
  }
  uint8_t values[2] = {0U, 0U};
  const uint8_t pair = pairForRegister(startReg);
  bool readCompleted = false;
  const Status status = pair == PAIR_INPUTS
      ? _readInputRegisters(startReg, values, len, readCompleted)
      : _readRegs(startReg, values, len, true);
  if (pair != PAIR_INPUTS) readCompleted = status.ok();
  if (!readCompleted) return status;
  for (size_t i = 0U; i < len; ++i) buf[i] = values[i];
  const uint32_t nowMs = _nowMs();
  if (len == 2U) {
    const uint16_t value = (startReg & 0x01U) == 0U
        ? static_cast<uint16_t>(values[0] |
            (static_cast<uint16_t>(values[1]) << 8U))
        : static_cast<uint16_t>(values[1] |
            (static_cast<uint16_t>(values[0]) << 8U));
    _recordPairObservation(pair, value, nowMs);
  } else {
    _syncObservedRegister(startReg, values[0], nowMs);
  }
  return status;
}

Status PCA9555::writeRegister(uint8_t reg, uint8_t value) {
  return writeRegisters(reg, &value, 1U);
}

Status PCA9555::writeRegisters(uint8_t startReg, const uint8_t* buf,
                               size_t len) {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (_operation.active) {
    return busy(BusyDetail::OPERATION_ACTIVE, "cooperative operation owns device");
  }
  if (!validRegister(startReg) || inputRegister(startReg) || buf == nullptr ||
      len == 0U || len > MAX_BULK_LEN) {
    return Status::Error(Err::INVALID_PARAM, "invalid advanced register write");
  }
  const uint8_t pair = pairForRegister(startReg);
  if (pair == PAIR_DIRECTIONS) {
    return Status::Error(Err::UNSUPPORTED,
                         "raw configuration writes bypass safe preload");
  }

  // Invalidate before the advanced attempt. A complete pair write establishes
  // a new shadow; a single-register write intentionally leaves the pair invalid.
  _shadowValidPairs = static_cast<uint8_t>(_shadowValidPairs & ~pair);
  uint16_t intended = 0U;
  if (len == 2U) {
    intended = (startReg & 0x01U) == 0U
        ? static_cast<uint16_t>(buf[0] |
            (static_cast<uint16_t>(buf[1]) << 8U))
        : static_cast<uint16_t>(buf[1] |
            (static_cast<uint16_t>(buf[0]) << 8U));
  }
  return _writeRegs(startReg, buf, len, pair, intended, len == 2U, true);
}

Status PCA9555::applyInterruptErrataWorkaround() {
  const Status bound = _boundStatus();
  if (!bound.ok()) return bound;
  if (_operation.active) {
    return busy(BusyDetail::OPERATION_ACTIVE, "cooperative operation owns device");
  }
  return _parkPointer();
}

SettingsSnapshot PCA9555::getSettings() const {
  SettingsSnapshot snapshot{};
  snapshot.state = _driverState;
  snapshot.initialized = _bound;
  snapshot.i2cAddress = _bound ? _config.i2cAddress : 0U;
  snapshot.lastOkMs = _lastOkMs;
  snapshot.lastErrorMs = _lastErrorMs;
  snapshot.lastError = _lastError;
  snapshot.consecutiveFailures = _consecutiveFailures;
  snapshot.totalFailures = _totalFailures;
  snapshot.totalSuccess = _totalSuccess;
  snapshot.shadowValidPairs = _shadowValidPairs;
  snapshot.uncertainPairs = _uncertainPairs;
  snapshot.observed = _observed;
  snapshot.observed.uncertainPairs = _uncertainPairs;
  return snapshot;
}

Status PCA9555::getSettings(SettingsSnapshot& out) const {
  out = getSettings();
  return Status::Ok();
}

}  // namespace PCA9555

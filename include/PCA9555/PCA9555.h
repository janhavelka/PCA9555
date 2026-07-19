/// @file PCA9555.h
/// @brief Passive, framework-neutral PCA9555 16-bit I/O expander driver.
#pragma once

#include <cstddef>
#include <cstdint>

#include "PCA9555/CommandTable.h"
#include "PCA9555/Config.h"
#include "PCA9555/Status.h"
#include "PCA9555/Version.h"

namespace PCA9555 {

enum class DriverState : uint8_t {
  UNINIT = 0,
  READY,
  DEGRADED
};

enum class Port : uint8_t { PORT_0 = 0, PORT_1 = 1 };

enum class Pin : uint8_t {
  P00 = 0, P01, P02, P03, P04, P05, P06, P07,
  P10, P11, P12, P13, P14, P15, P16, P17
};

enum class Level : uint8_t { LOW_LEVEL = 0, HIGH_LEVEL = 1 };

enum class Direction : uint8_t {
  INPUT_MODE = 0,
  OUTPUT_MODE = 1
};

using PinMask = uint16_t;

/// Register-pair evidence bits used by ObservedState and OperationResult.
enum StatePair : uint8_t {
  PAIR_NONE = 0,
  PAIR_INPUTS = 1U << 0,
  PAIR_OUTPUTS = 1U << 1,
  PAIR_POLARITY = 1U << 2,
  PAIR_DIRECTIONS = 1U << 3,
  PAIR_ALL_WRITABLE = PAIR_OUTPUTS | PAIR_POLARITY | PAIR_DIRECTIONS,
  PAIR_ALL = PAIR_INPUTS | PAIR_ALL_WRITABLE
};

struct PortData {
  uint8_t port0 = 0;
  uint8_t port1 = 0;

  constexpr uint16_t combined() const {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(port0) |
        (static_cast<uint16_t>(port1) << 8U));
  }

  static constexpr PortData fromCombined(uint16_t value) {
    return PortData{static_cast<uint8_t>(value & 0xFFU),
                    static_cast<uint8_t>((value >> 8U) & 0xFFU)};
  }
};

/// Complete caller-owned writable PCA9555 state.
struct RegisterImage {
  uint16_t outputs = 0xFFFFU;
  uint16_t polarity = 0x0000U;
  uint16_t directions = 0xFFFFU;  ///< One means input, matching the chip.
};

/// Hardware observations. Validity is explicit and reads never change intent.
struct ObservedState {
  RegisterImage registers{};
  uint16_t inputs = 0;
  uint8_t validPairs = PAIR_NONE;  ///< Complete pairs observed in one transfer.
  /// Pairs proven different from the comparison basis: protocol shadow for
  /// driver snapshots, or caller expectation inside OperationResult.
  uint8_t mismatchPairs = PAIR_NONE;
  uint8_t uncertainPairs = PAIR_NONE;  ///< Pairs with ambiguous write effects.
  uint32_t observedAtMs = 0;

  constexpr bool valid(StatePair pair) const {
    const uint8_t mask = static_cast<uint8_t>(pair);
    return mask != PAIR_NONE && (validPairs & mask) == mask;
  }
};

constexpr uint8_t pinIndex(Pin pin) { return static_cast<uint8_t>(pin); }
constexpr bool isValidPin(Pin pin) { return pinIndex(pin) < 16U; }
constexpr PinMask pinMask(Pin pin) {
  return isValidPin(pin)
             ? static_cast<PinMask>(static_cast<PinMask>(1U) << pinIndex(pin))
             : static_cast<PinMask>(0U);
}
constexpr Port portOf(Pin pin) {
  return pinIndex(pin) < 8U ? Port::PORT_0 : Port::PORT_1;
}
constexpr uint8_t bitOf(Pin pin) {
  return static_cast<uint8_t>(pinIndex(pin) & 0x07U);
}
constexpr bool isValidAddress(uint8_t address) {
  return address >= 0x20U && address <= 0x27U;
}
constexpr bool isOutput(const RegisterImage& image, Pin pin) {
  return isValidPin(pin) && (image.directions & pinMask(pin)) == 0U;
}
constexpr Level levelFor(const RegisterImage& image, Pin pin) {
  return (image.outputs & pinMask(pin)) != 0U ? Level::HIGH_LEVEL
                                              : Level::LOW_LEVEL;
}

enum class OperationKind : uint8_t {
  NONE = 0,
  APPLY_IMAGE,
  VERIFY_IMAGE,
  READ_INPUTS
};

enum class OperationPhase : uint8_t {
  NONE = 0,
  APPLY_OUTPUTS,
  APPLY_POLARITY,
  APPLY_DIRECTIONS,
  VERIFY_OUTPUTS,
  VERIFY_POLARITY,
  VERIFY_DIRECTIONS,
  READ_INPUTS,
  POINTER_PARK,
  COMPLETE
};

enum class OperationOutcome : uint8_t {
  NONE = 0,
  ACTIVE,
  SUCCEEDED,
  FAILED,
  CANCELLED,
  TIMED_OUT,
  MISMATCH,
  INDETERMINATE
};

static constexpr uint8_t MAX_APPLY_IMAGE_TRANSACTIONS = 8U;
static constexpr uint8_t MAX_VERIFY_IMAGE_TRANSACTIONS = 3U;
static constexpr uint8_t MAX_READ_INPUTS_TRANSACTIONS = 2U;

/// Exactly-once terminal evidence retained by a cooperative operation.
struct OperationResult {
  uint32_t requestId = 0;
  OperationKind kind = OperationKind::NONE;
  OperationOutcome outcome = OperationOutcome::NONE;
  OperationPhase terminalPhase = OperationPhase::NONE;
  Status status = Status::Ok();
  Status cleanupStatus = Status::Ok();
  /// Effect of the most recent apply-image register write. Verification reads
  /// and the pointer-park command do not replace this evidence.
  WriteEffect lastWriteEffect = WriteEffect::NOT_APPLICABLE;
  uint8_t transactionsUsed = 0;
  /// Register pairs whose read or write phase completed in this operation.
  uint8_t completedPairs = PAIR_NONE;
  uint8_t shadowValidPairs = PAIR_NONE;  ///< Protocol write-shadow validity.
  uint8_t mismatchPairs = PAIR_NONE;
  uint8_t uncertainPairs = PAIR_NONE;
  bool cleanupRequired = false;
  bool cleanupAttempted = false;
  /// True only when required pointer cleanup was polled at or after the stored
  /// whole-operation deadline. An earlier explicit timeout request stays false.
  bool cleanupAfterDeadline = false;
  ObservedState observed{};

  constexpr bool terminal() const {
    return outcome != OperationOutcome::NONE &&
           outcome != OperationOutcome::ACTIVE;
  }
};

struct SettingsSnapshot {
  DriverState state = DriverState::UNINIT;
  bool initialized = false;  ///< Compatibility spelling for bound state.
  uint8_t i2cAddress = 0;
  uint32_t lastOkMs = 0;
  uint32_t lastErrorMs = 0;
  Status lastError = Status::Ok();
  uint8_t consecutiveFailures = 0;
  uint32_t totalFailures = 0;
  uint32_t totalSuccess = 0;
  uint8_t shadowValidPairs = PAIR_NONE;
  uint8_t uncertainPairs = PAIR_NONE;
  ObservedState observed{};
};

/// Non-owning, single-threaded and non-reentrant driver. Public I2C methods are
/// not ISR-safe. The caller owns serialization, scheduling, transaction timeout,
/// retry eligibility and policy, health policy, and bus recovery.
class PCA9555 {
 public:
  PCA9555() = default;
  PCA9555(const PCA9555&) = delete;
  PCA9555& operator=(const PCA9555&) = delete;
  PCA9555(PCA9555&&) = delete;
  PCA9555& operator=(PCA9555&&) = delete;

  // Passive lifecycle. A failed replacement preserves the existing binding.
  Status bind(const Config& config);
  Status begin(const Config& config) { return bind(config); }
  Status detach();
  Status end() { return detach(); }
  bool isInitialized() const { return _bound; }
  bool isBound() const { return _bound; }
  DriverState state() const { return _driverState; }
  DriverState driverState() const { return _driverState; }
  const Config& getConfig() const { return _config; }

  // Explicit diagnostics.
  Status probe();
  Status checkPorDefaults(PortData& observedDirections);
  /// Advanced synchronous writable-register snapshot. This performs at most
  /// three callbacks and can block for up to 3 * Config::i2cTimeoutMs. Use
  /// startVerifyImage() when the external owner needs transaction budgeting.
  /// On failure, observed contains only pairs read during this invocation.
  Status readObservedState(ObservedState& observed);
  const ObservedState& lastObservedState() const { return _observed; }

  // Cooperative operations. Admission performs zero I2C. timeoutMs is a
  // whole-operation duration and must be 1..INT32_MAX. A terminal result blocks
  // new admission until consumed exactly once.
  Status startApplyImage(uint32_t requestId, const RegisterImage& image,
                         uint32_t nowMs, uint32_t timeoutMs);
  Status startVerifyImage(uint32_t requestId, const RegisterImage& expected,
                          uint32_t nowMs, uint32_t timeoutMs);
  Status startReadInputs(uint32_t requestId, uint32_t nowMs,
                         uint32_t timeoutMs);
  Status pollOperation(uint32_t requestId, uint32_t nowMs,
                       uint8_t transactionBudget, uint8_t& transactionsUsed);
  Status cancelOperation(uint32_t requestId);
  Status timeoutOperation(uint32_t requestId);
  Status takeOperationResult(uint32_t requestId, OperationResult& result);
  bool operationActive() const { return _operation.active; }
  bool operationResultPending() const { return _operation.resultPending; }
  uint32_t activeRequestId() const { return _operation.requestId; }
  OperationPhase operationPhase() const { return _operation.phase; }

  // Synchronous owner-safe primitives. Plain register reads/writes use one
  // callback. Input APIs use at most two (read then pointer park). Safe
  // direction APIs use at most two (latch preload then direction write).
  Status readInputs(PortData& data);
  Status readInputsAndClearInterrupt(uint16_t& value);
  Status clearInterrupts();
  Status readInput(Port port, uint8_t& value);
  Status readPin(Pin pin, Level& level);
  Status readPin(Pin pin, bool& high);

  Status writeOutputs(const PortData& data);
  Status writeOutput(Port port, uint8_t value);
  Status readOutputs(PortData& data);
  Status readOutput(Port port, uint8_t& value);
  Status writePin(Pin pin, Level level);
  Status writePin(Pin pin, bool high);
  Status readOutputPin(Pin pin, Level& level);
  Status readOutputPin(Pin pin, bool& high);
  Status preloadOutput(Pin pin, Level level);
  Status preloadOutput(Pin pin, bool high);
  Status preloadOutputs(PinMask mask, PinMask values);
  Status setOutputBits(PinMask mask);
  Status clearOutputBits(PinMask mask);
  Status toggleOutputBits(PinMask mask);
  Status togglePin(Pin pin);

  Status setConfiguration(const PortData& data);
  Status setPortConfiguration(Port port, uint8_t value);
  Status getConfiguration(PortData& data);
  Status getPortConfiguration(Port port, uint8_t& value);
  Status configureOutputs(PinMask outputMask, PinMask outputValues);
  Status configureInputBits(PinMask mask);
  Status configureOutputBits(PinMask mask);
  Status setDirection(Pin pin, Direction direction);
  Status setPinDirection(Pin pin, bool input);
  Status getPinDirection(Pin pin, Direction& direction);
  Status getPinDirection(Pin pin, bool& input);

  Status setPolarity(const PortData& data);
  Status setPortPolarity(Port port, uint8_t value);
  Status getPolarity(PortData& data);
  Status getPortPolarity(Port port, uint8_t& value);
  Status setPinPolarity(Pin pin, bool inverted);
  Status getPinPolarity(Pin pin, bool& inverted);
  Status setInvertBits(PinMask mask);
  Status clearInvertBits(PinMask mask);

  // Advanced diagnostics. Reads update only ObservedState. Raw writes to
  // Configuration registers are rejected because they bypass safe preload.
  // Other raw writes invalidate their whole protocol-shadow pair before the
  // attempt and revalidate it only after a complete two-register write.
  Status readRegister(uint8_t reg, uint8_t& value);
  Status readRegisters(uint8_t startReg, uint8_t* buf, size_t len);
  Status writeRegister(uint8_t reg, uint8_t value);
  Status writeRegisters(uint8_t startReg, const uint8_t* buf, size_t len);

  // Explicit errata primitive for diagnostics. Production code should use an
  // input API so the owner keeps read and park non-interleaved.
  Status applyInterruptErrataWorkaround();

  SettingsSnapshot getSettings() const;
  Status getSettings(SettingsSnapshot& out) const;
  uint32_t lastOkMs() const { return _lastOkMs; }
  uint32_t lastErrorMs() const { return _lastErrorMs; }
  Status lastError() const { return _lastError; }
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  uint32_t totalFailures() const { return _totalFailures; }
  uint32_t totalSuccess() const { return _totalSuccess; }
  uint8_t shadowValidPairs() const { return _shadowValidPairs; }
  uint8_t uncertainPairs() const { return _uncertainPairs; }

 private:
  struct OperationSlot {
    bool active = false;
    bool resultPending = false;
    bool terminalRequested = false;
    uint32_t requestId = 0;
    uint32_t deadlineMs = 0;
    OperationKind kind = OperationKind::NONE;
    OperationPhase phase = OperationPhase::NONE;
    OperationOutcome requestedOutcome = OperationOutcome::NONE;
    OperationPhase requestedTerminalPhase = OperationPhase::NONE;
    Status requestedStatus = Status::Ok();
    RegisterImage expected{};
    OperationResult result{};
  };

  Status _validateBinding(const Config& config) const;
  Status _boundStatus() const;
  Status _shadowStatus(uint8_t pairs) const;
  bool _deadlineReached(uint32_t nowMs) const;
  Status _startOperation(OperationKind kind, uint32_t requestId,
                         const RegisterImage& expected, uint32_t nowMs,
                         uint32_t timeoutMs, OperationPhase firstPhase);
  Status _executeOperationTransfer(uint32_t nowMs);
  void _finishOperation(OperationOutcome outcome, const Status& status,
                        OperationPhase terminalPhase);
  Status _requestTerminal(uint32_t requestId, OperationOutcome outcome,
                          const Status& status);
  void _recordPairObservation(uint8_t pair, uint16_t value, uint32_t nowMs);
  void _compareObserved(uint8_t pair, uint16_t expected);
  Status _readRegs(uint8_t startReg, uint8_t* buf, size_t len, bool tracked,
                   WriteEffect* commandEffect = nullptr);
  Status _writeRegs(uint8_t startReg, const uint8_t* buf, size_t len,
                    uint8_t affectedPair, uint16_t intended,
                    bool establishWholePair, bool tracked);
  Status _readInputPair(PortData& data, bool& readCompleted);
  Status _readInputRegisters(uint8_t startReg, uint8_t* buf, size_t len,
                             bool& readCompleted);
  Status _readPin(Pin pin, Level& level, bool& readCompleted);
  Status _parkPointer();
  Status _readPair(uint8_t startReg, uint16_t& value, uint8_t pair,
                   uint32_t nowMs);
  Status _writePair(uint8_t startReg, uint16_t value, uint8_t pair,
                    bool tracked = true);
  Status _writePort(uint8_t reg, uint8_t value, uint8_t pair,
                    uint16_t intendedCombined);

  Status _mapTransportResult(const TransportResult& result,
                             size_t expectedTx, size_t expectedRx,
                             bool registerWrite, WriteEffect& effect) const;
  Status _i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                          uint8_t* rxBuf, size_t rxLen,
                          WriteEffect& commandEffect);
  Status _i2cWriteRaw(const uint8_t* buf, size_t len,
                      WriteEffect& effect);
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                              uint8_t* rxBuf, size_t rxLen,
                              WriteEffect& commandEffect);
  Status _i2cWriteTracked(const uint8_t* buf, size_t len,
                          WriteEffect& effect);
  Status _updateHealth(const Status& status);
  uint32_t _nowMs() const;
  void _syncObservedRegister(uint8_t reg, uint8_t value, uint32_t nowMs);
  void _invalidateShadowPair(uint8_t pair);
  void _establishShadowPair(uint8_t pair, uint16_t value);
  uint16_t _shadowValue(uint8_t pair) const;

  Config _config{};
  bool _bound = false;
  DriverState _driverState = DriverState::UNINIT;

  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;

  RegisterImage _shadow{};
  uint8_t _shadowValidPairs = PAIR_NONE;
  uint8_t _uncertainPairs = PAIR_NONE;
  ObservedState _observed{};
  WriteEffect _lastWriteEffect = WriteEffect::NOT_APPLICABLE;

  OperationSlot _operation{};
  uint32_t _callbackTimeoutMs = 0;
  bool _operationTimeoutActive = false;
  bool _insideOperationTransfer = false;
};

}  // namespace PCA9555

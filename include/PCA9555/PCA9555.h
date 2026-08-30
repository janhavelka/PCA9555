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

/// Passive binding and latest tracked-transfer health state.
enum class DriverState : uint8_t {
  UNINIT = 0,
  READY,
  DEGRADED
};

/// Physical PCA9555 8-bit port selector.
enum class Port : uint8_t { PORT_0 = 0, PORT_1 = 1 };

/// Data-sheet pin names in linear bit order from P00 through P17.
enum class Pin : uint8_t {
  P00 = 0, P01, P02, P03, P04, P05, P06, P07,
  P10, P11, P12, P13, P14, P15, P16, P17
};

/// Typed binary pin or output-latch level.
enum class Level : uint8_t { LOW_LEVEL = 0, HIGH_LEVEL = 1 };

/// Typed pin direction; PCA9555 configuration bits use the inverse encoding.
enum class Direction : uint8_t {
  INPUT_MODE = 0,
  OUTPUT_MODE = 1
};

/// Sixteen-bit mask with bit 0=P00 and bit 15=P17.
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

/// Two-port byte representation of one complete PCA9555 register pair.
struct PortData {
  uint8_t port0 = 0;  ///< Port 0 byte, mapped to combined bits 0 through 7.
  uint8_t port1 = 0;  ///< Port 1 byte, mapped to combined bits 8 through 15.

  /// Combine Port 0 as the low byte and Port 1 as the high byte.
  constexpr uint16_t combined() const {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(port0) |
        (static_cast<uint16_t>(port1) << 8U));
  }

  /// Split a 16-bit value into low-byte Port 0 and high-byte Port 1 values.
  static constexpr PortData fromCombined(uint16_t value) {
    return PortData{static_cast<uint8_t>(value & 0xFFU),
                    static_cast<uint8_t>((value >> 8U) & 0xFFU)};
  }
};

/// Complete caller-owned writable PCA9555 state.
struct RegisterImage {
  uint16_t outputs = 0xFFFFU;   ///< Output-latch pair, high by default.
  uint16_t polarity = 0x0000U;  ///< Input polarity pair; one means inverted.
  uint16_t directions = 0xFFFFU;  ///< One means input, matching the chip.
};

/// Hardware observations. Validity is explicit and reads never change intent.
struct ObservedState {
  RegisterImage registers{};  ///< Latest observed complete writable pairs.
  uint16_t inputs = 0;        ///< Latest observed complete input pair.
  uint8_t validPairs = PAIR_NONE;  ///< Complete pairs observed in one transfer.
  uint8_t mismatchPairs = PAIR_NONE;  ///< Pairs differing from the comparison basis.
  uint8_t uncertainPairs = PAIR_NONE;  ///< Driver-wide ambiguous-write evidence.
  uint32_t observedAtMs = 0;  ///< Timestamp supplied by Config::nowMs.

  /// Return true when every bit in one requested pair mask is valid.
  constexpr bool valid(StatePair pair) const {
    const uint8_t mask = static_cast<uint8_t>(pair);
    return mask != PAIR_NONE && (validPairs & mask) == mask;
  }
};

/// Convert a typed pin to its linear bit index.
constexpr uint8_t pinIndex(Pin pin) { return static_cast<uint8_t>(pin); }
/// Return true when pin names one of the sixteen PCA9555 pins.
constexpr bool isValidPin(Pin pin) { return pinIndex(pin) < 16U; }
/// Return the bit mask for a valid pin, or zero for an invalid value.
constexpr PinMask pinMask(Pin pin) {
  return isValidPin(pin)
             ? static_cast<PinMask>(static_cast<PinMask>(1U) << pinIndex(pin))
             : static_cast<PinMask>(0U);
}
/// Return the physical port containing a valid pin.
constexpr Port portOf(Pin pin) {
  return pinIndex(pin) < 8U ? Port::PORT_0 : Port::PORT_1;
}
/// Return the bit index within the physical port containing a valid pin.
constexpr uint8_t bitOf(Pin pin) {
  return static_cast<uint8_t>(pinIndex(pin) & 0x07U);
}
/// Return true for a legal PCA9555 7-bit address.
constexpr bool isValidAddress(uint8_t address) {
  return address >= 0x20U && address <= 0x27U;
}
/// Return true when a valid pin is configured as an output in image.
constexpr bool isOutput(const RegisterImage& image, Pin pin) {
  return isValidPin(pin) && (image.directions & pinMask(pin)) == 0U;
}
/// Return the output-latch level selected for pin in image.
constexpr Level levelFor(const RegisterImage& image, Pin pin) {
  return (image.outputs & pinMask(pin)) != 0U ? Level::HIGH_LEVEL
                                              : Level::LOW_LEVEL;
}

/// Cooperative compound operation type.
enum class OperationKind : uint8_t {
  NONE = 0,
  APPLY_IMAGE,
  VERIFY_IMAGE,
  READ_INPUTS
};

/// Observable protocol phase for cooperative operations.
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

/// Terminal or active cooperative-operation disposition.
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

/// Maximum callbacks used by one complete apply-image operation.
static constexpr uint8_t MAX_APPLY_IMAGE_TRANSACTIONS = 8U;
/// Maximum callbacks used by one complete verify-image operation.
static constexpr uint8_t MAX_VERIFY_IMAGE_TRANSACTIONS = 3U;
/// Maximum callbacks used by one complete cooperative input operation.
static constexpr uint8_t MAX_READ_INPUTS_TRANSACTIONS = 2U;

/// Exactly-once terminal evidence retained by a cooperative operation.
/// outcome and status describe the whole operation, including input read and
/// pointer cleanup. For APPLY_IMAGE, a terminal READ_INPUTS or POINTER_PARK
/// phase is reached only after all writable pairs were read back and matched;
/// completedPairs and mismatchPairs retain that independently useful evidence.
struct OperationResult {
  uint32_t requestId = 0;  ///< Exact caller-supplied correlation ID.
  OperationKind kind = OperationKind::NONE;  ///< Admitted operation type.
  OperationOutcome outcome = OperationOutcome::NONE;  ///< Final disposition.
  OperationPhase terminalPhase = OperationPhase::NONE;  ///< Primary terminal phase.
  Status status = Status::Ok();  ///< Primary terminal status.
  Status cleanupStatus = Status::Ok();  ///< Pointer-park result when attempted.
  WriteEffect lastWriteEffect = WriteEffect::NOT_APPLICABLE;  ///< Latest apply-write effect.
  uint8_t transactionsUsed = 0;  ///< Total callbacks consumed by the operation.
  uint8_t completedPairs = PAIR_NONE;  ///< Pairs completed by this operation.
  uint8_t shadowValidPairs = PAIR_NONE;  ///< Protocol write-shadow validity.
  uint8_t mismatchPairs = PAIR_NONE;  ///< Pairs differing from caller expectation.
  uint8_t uncertainPairs = PAIR_NONE;  ///< Pairs with ambiguous write effects.
  bool cleanupRequired = false;  ///< Pointer park remains required after result.
  bool cleanupAttempted = false;  ///< A pointer-park callback was invoked.
  bool cleanupAfterDeadline = false;  ///< Cleanup polled at/after stored deadline.
  ObservedState observed{};  ///< Observations collected by this operation.

  /// Return true only for a final, consumable outcome.
  constexpr bool terminal() const {
    return outcome != OperationOutcome::NONE &&
           outcome != OperationOutcome::ACTIVE;
  }
};

/// By-value diagnostic snapshot of binding, health, and protocol evidence.
struct SettingsSnapshot {
  DriverState state = DriverState::UNINIT;  ///< Current driver state.
  bool initialized = false;  ///< Compatibility spelling for bound state.
  uint8_t i2cAddress = 0;  ///< Bound address, or zero while unbound.
  uint32_t lastOkMs = 0;  ///< Timestamp of latest tracked success.
  uint32_t lastErrorMs = 0;  ///< Timestamp of latest tracked failure.
  Status lastError = Status::Ok();  ///< Latest tracked failure status.
  uint8_t consecutiveFailures = 0;  ///< Failures since latest tracked success.
  uint32_t totalFailures = 0;  ///< Saturating lifetime tracked failure count.
  uint32_t totalSuccess = 0;  ///< Saturating lifetime tracked success count.
  uint8_t shadowValidPairs = PAIR_NONE;  ///< Cached-RMW-safe writable pairs.
  uint8_t uncertainPairs = PAIR_NONE;  ///< Pairs with ambiguous write effects.
  ObservedState observed{};  ///< Latest hardware observations.
};

/// Non-owning, single-threaded and non-reentrant driver. Public I2C methods are
/// not ISR-safe. The caller owns serialization, scheduling, transaction timeout,
/// retry eligibility and policy, health policy, and bus recovery.
class PCA9555 {
 public:
  /// Construct an unbound driver without performing I2C.
  PCA9555() = default;
  /// Driver instances are not copyable because they retain one operation slot.
  PCA9555(const PCA9555&) = delete;
  /// Driver instances are not copy-assignable.
  PCA9555& operator=(const PCA9555&) = delete;
  /// Driver instances are not movable because callbacks may reference stable storage.
  PCA9555(PCA9555&&) = delete;
  /// Driver instances are not move-assignable.
  PCA9555& operator=(PCA9555&&) = delete;

  /// Validate and store a passive binding with zero I2C.
  /// A failed replacement preserves the existing valid binding.
  Status bind(const Config& config);
  /// Compatibility alias for bind(); performs zero I2C.
  Status begin(const Config& config) { return bind(config); }
  /// Remove the binding with zero I2C when no pointer cleanup is owed.
  /// Ordinary active work becomes a retained cancellation result. A pending
  /// result remains exactly-once evidence; use activeRequestId() and
  /// takeOperationResult() before replacing the binding.
  Status detach();
  /// Compatibility alias for detach(); performs zero I2C.
  Status end() { return detach(); }
  /// Compatibility spelling for isBound().
  bool isInitialized() const { return _bound; }
  /// Return true when a valid Config is stored; device presence is not implied.
  bool isBound() const { return _bound; }
  /// Return the current passive binding and tracked-transfer state.
  DriverState state() const { return _driverState; }
  /// Compatibility spelling for state().
  DriverState driverState() const { return _driverState; }
  /// Return the stored binding configuration.
  const Config& getConfig() const { return _config; }

  /// Perform one raw, nonzero command-byte write without changing health counters.
  Status probe();
  /// Read both Configuration registers and report whether they match POR defaults.
  Status checkPorDefaults(PortData& observedDirections);
  /// Advanced synchronous writable-register snapshot. This performs at most
  /// three callbacks and can block for up to 3 * Config::i2cTimeoutMs. Use
  /// startVerifyImage() when the external owner needs transaction budgeting.
  /// On failure, register values, validPairs, and mismatchPairs contain only
  /// pairs read during this invocation. uncertainPairs is driver-wide evidence
  /// at return time and can include pairs not valid in this sample.
  Status readObservedState(ObservedState& observed);
  /// Return the latest retained hardware observations.
  const ObservedState& lastObservedState() const { return _observed; }

  /// Admit a bounded write-and-verify of a complete caller-owned image.
  /// Admission performs zero I2C and returns Err::IN_PROGRESS on success;
  /// timeoutMs must be 1 through INT32_MAX. The operation writes and reads back
  /// all three writable pairs, then reads both input ports and parks the command
  /// pointer, so it also clears both pending interrupt sources.
  Status startApplyImage(uint32_t requestId, const RegisterImage& image,
                         uint32_t nowMs, uint32_t timeoutMs);
  /// Admit a bounded readback comparison against a complete caller image.
  /// Admission performs zero I2C and returns Err::IN_PROGRESS on success.
  /// Reads only; it does not touch the input ports or the command pointer.
  Status startVerifyImage(uint32_t requestId, const RegisterImage& expected,
                          uint32_t nowMs, uint32_t timeoutMs);
  /// Admit an exclusive two-port input read and required pointer park.
  /// Admission performs zero I2C and returns Err::IN_PROGRESS on success.
  Status startReadInputs(uint32_t requestId, uint32_t nowMs,
                         uint32_t timeoutMs);
  /// Advance the matching operation by at most transactionBudget callbacks.
  /// Returns Err::IN_PROGRESS while work remains, otherwise the terminal status.
  Status pollOperation(uint32_t requestId, uint32_t nowMs,
                       uint8_t transactionBudget, uint8_t& transactionsUsed);
  /// Request cooperative cancellation of the matching active operation.
  /// Returns Status::Ok() once the result is terminal, or Err::IN_PROGRESS while
  /// a required pointer park still owes one bounded pollOperation() call.
  Status cancelOperation(uint32_t requestId);
  /// Request a caller-forced cooperative timeout of the matching operation.
  /// Returns Status::Ok() once the result is terminal, or Err::IN_PROGRESS while
  /// a required pointer park still owes one bounded pollOperation() call.
  Status timeoutOperation(uint32_t requestId);
  /// Consume the matching terminal result exactly once.
  Status takeOperationResult(uint32_t requestId, OperationResult& result);
  /// Return true while a cooperative operation still owns the device.
  bool operationActive() const { return _operation.active; }
  /// Return true while an unconsumed terminal result blocks new admission.
  bool operationResultPending() const { return _operation.resultPending; }
  /// Return the active or pending request ID, or zero when the slot is empty.
  uint32_t activeRequestId() const { return _operation.requestId; }
  /// Return the current cooperative protocol phase.
  OperationPhase operationPhase() const { return _operation.phase; }

  /// Read both input ports and then perform the mandatory pointer park.
  Status readInputs(PortData& data);
  /// Read both input ports, clear both interrupt sources, and return 16 bits.
  Status readInputsAndClearInterrupt(uint16_t& value);
  /// Read and discard both input ports, then perform the pointer park.
  Status clearInterrupts();
  /// Read one input port, then perform the pointer park.
  Status readInput(Port port, uint8_t& value);
  /// Read one pin's input sense, then perform the pointer park.
  Status readPin(Pin pin, Level& level);
  /// Boolean compatibility overload for readPin().
  Status readPin(Pin pin, bool& high);

  /// Write the complete two-port output-latch pair in one callback.
  Status writeOutputs(const PortData& data);
  /// Write one port's output latch without adopting the other port as intent.
  Status writeOutput(Port port, uint8_t value);
  /// Read the complete output-latch pair as observation.
  Status readOutputs(PortData& data);
  /// Read one port's output latch as observation.
  Status readOutput(Port port, uint8_t& value);
  /// Update one output-latch bit using a valid, certain output shadow.
  /// Reasserts the complete pair even when the requested level already matches.
  Status writePin(Pin pin, Level level);
  /// Boolean compatibility overload for writePin().
  Status writePin(Pin pin, bool high);
  /// Read one output-latch bit; this is not physical pin-voltage proof.
  Status readOutputPin(Pin pin, Level& level);
  /// Boolean compatibility overload for readOutputPin().
  Status readOutputPin(Pin pin, bool& high);
  /// Set one output-latch bit without changing direction.
  Status preloadOutput(Pin pin, Level level);
  /// Boolean compatibility overload for preloadOutput().
  Status preloadOutput(Pin pin, bool high);
  /// Set selected output-latch bits without changing direction.
  Status preloadOutputs(PinMask mask, PinMask values);
  /// Set selected output-latch bits using the cached output shadow.
  /// A nonzero mask reasserts the complete pair even when all bits already match.
  Status setOutputBits(PinMask mask);
  /// Clear selected output-latch bits using the cached output shadow.
  /// A nonzero mask reasserts the complete pair even when all bits already match.
  Status clearOutputBits(PinMask mask);
  /// Toggle selected output-latch bits using the cached output shadow.
  Status toggleOutputBits(PinMask mask);
  /// Toggle one output-latch bit using the cached output shadow.
  Status togglePin(Pin pin);

  /// Safely write both direction bytes, preloading latches before any output enable.
  Status setConfiguration(const PortData& data);
  /// Safely write one direction byte, preloading its latch before output enable.
  Status setPortConfiguration(Port port, uint8_t value);
  /// Read the complete direction pair as observation.
  Status getConfiguration(PortData& data);
  /// Read one port's direction byte as observation.
  Status getPortConfiguration(Port port, uint8_t& value);
  /// Preload selected latch values, then enable those pins as outputs.
  Status configureOutputs(PinMask outputMask, PinMask outputValues);
  /// Configure selected pins as inputs using a valid direction shadow.
  /// A nonzero mask reasserts the complete pair even when all bits already match.
  Status configureInputBits(PinMask mask);
  /// Enable selected pins as outputs after safely preloading known latches.
  Status configureOutputBits(PinMask mask);
  /// Change one pin direction with latch-before-output-enable ordering.
  Status setDirection(Pin pin, Direction direction);
  /// Boolean compatibility overload; true means input.
  Status setPinDirection(Pin pin, bool input);
  /// Read one pin direction into the typed result.
  Status getPinDirection(Pin pin, Direction& direction);
  /// Boolean compatibility overload; true means input.
  Status getPinDirection(Pin pin, bool& input);

  /// Write the complete two-port input-polarity pair.
  Status setPolarity(const PortData& data);
  /// Write one port's input-polarity byte.
  Status setPortPolarity(Port port, uint8_t value);
  /// Read the complete input-polarity pair as observation.
  Status getPolarity(PortData& data);
  /// Read one port's input-polarity byte as observation.
  Status getPortPolarity(Port port, uint8_t& value);
  /// Set or clear one polarity bit using a valid, certain polarity shadow.
  /// Reasserts the complete pair even when the requested value already matches.
  Status setPinPolarity(Pin pin, bool inverted);
  /// Read one pin's input-polarity setting.
  Status getPinPolarity(Pin pin, bool& inverted);
  /// Set selected input-polarity bits using the cached polarity shadow.
  /// A nonzero mask reasserts the complete pair even when all bits already match.
  Status setInvertBits(PinMask mask);
  /// Clear selected input-polarity bits using the cached polarity shadow.
  /// A nonzero mask reasserts the complete pair even when all bits already match.
  Status clearInvertBits(PinMask mask);

  /// Read one register as observation; input reads also park the pointer.
  Status readRegister(uint8_t reg, uint8_t& value);
  /// Read one or two registers within a pair as observation.
  Status readRegisters(uint8_t startReg, uint8_t* buf, size_t len);
  /// Write one Output or Polarity register; Configuration writes are rejected.
  Status writeRegister(uint8_t reg, uint8_t value);
  /// Write one or two pair-local Output or Polarity registers.
  Status writeRegisters(uint8_t startReg, const uint8_t* buf, size_t len);

  /// Perform only the nonzero pointer-park command for advanced diagnostics.
  Status applyInterruptErrataWorkaround();

  /// Return a by-value diagnostic snapshot without I2C.
  SettingsSnapshot getSettings() const;
  /// Store a diagnostic snapshot and return success without I2C.
  Status getSettings(SettingsSnapshot& out) const;
  /// Return the latest tracked-success timestamp.
  uint32_t lastOkMs() const { return _lastOkMs; }
  /// Return the latest tracked-failure timestamp.
  uint32_t lastErrorMs() const { return _lastErrorMs; }
  /// Return the latest tracked-failure status.
  Status lastError() const { return _lastError; }
  /// Return the saturating failures-since-success count.
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }
  /// Return the saturating lifetime tracked-failure count.
  uint32_t totalFailures() const { return _totalFailures; }
  /// Return the saturating lifetime tracked-success count.
  uint32_t totalSuccess() const { return _totalSuccess; }
  /// Return writable-pair bits safe for cached read-modify-write helpers.
  uint8_t shadowValidPairs() const { return _shadowValidPairs; }
  /// Return writable-pair bits with ambiguous hardware effects.
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

  static Status _validateBinding(const Config& config);
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
  Status _readPair(uint8_t startReg, uint16_t& value, uint32_t nowMs);
  Status _writePair(uint8_t startReg, uint16_t value, bool tracked = true);
  Status _writePort(uint8_t reg, uint8_t value, uint8_t pair,
                    uint16_t intendedCombined);
  Status _readWritablePortRegister(uint8_t baseReg, Port port, uint8_t& value);

  static Status _mapTransportResult(const TransportResult& result,
                                    size_t expectedTx, size_t expectedRx,
                                    bool registerWrite, WriteEffect& effect);
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
  Status _i2cWriteCleanupTracked(const uint8_t* buf, size_t len,
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

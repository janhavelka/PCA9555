#include <cstddef>
#include <cstdint>

#include <PCA9555/PCA9555.h>

namespace {

constexpr uint8_t DEVICE_ADDRESS = 0x20U;
constexpr uint32_t OWNER_CALLBACK_TIMEOUT_MS = 5U;

struct OwnerTransport {
  uint8_t registers[8] = {0x5AU, 0xA5U, 0xFFU, 0xFFU,
                          0x00U, 0x00U, 0xFFU, 0xFFU};
  uint32_t callbackCount = 0U;
  bool invalidCallback = false;
  bool failNextRead = false;
};

bool validAttempt(OwnerTransport& owner, uint8_t address,
                  const uint8_t* data, std::size_t length,
                  uint32_t timeoutMs) {
  ++owner.callbackCount;
  const bool valid = address == DEVICE_ADDRESS && data != nullptr &&
                     length != 0U && timeoutMs == OWNER_CALLBACK_TIMEOUT_MS;
  if (!valid) owner.invalidCallback = true;
  return valid;
}

PCA9555::TransportResult writeI2c(uint8_t address,
                                  const uint8_t* data,
                                  std::size_t length,
                                  uint32_t timeoutMs,
                                  void* context) {
  auto& owner = *static_cast<OwnerTransport*>(context);
  if (!validAttempt(owner, address, data, length, timeoutMs)) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 1,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }

  if (length == 1U) {
    return PCA9555::TransportResult::Ok(length, 0U);
  }
  const uint8_t firstRegister = data[0];
  if (firstRegister >= 8U || length - 1U > 8U - firstRegister) {
    owner.invalidCallback = true;
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 2,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  for (std::size_t i = 1U; i < length; ++i) {
    owner.registers[firstRegister + i - 1U] = data[i];
  }
  return PCA9555::TransportResult::Ok(length, 0U);
}

PCA9555::TransportResult writeReadI2c(uint8_t address,
                                      const uint8_t* write,
                                      std::size_t writeLength,
                                      uint8_t* read,
                                      std::size_t readLength,
                                      uint32_t timeoutMs,
                                      void* context) {
  auto& owner = *static_cast<OwnerTransport*>(context);
  if (!validAttempt(owner, address, write, writeLength, timeoutMs) ||
      read == nullptr || readLength == 0U || writeLength != 1U) {
    owner.invalidCallback = true;
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 3,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  if (owner.failNextRead) {
    owner.failNextRead = false;
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::NACK_ADDRESS, 4,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }

  const uint8_t firstRegister = write[0];
  if (firstRegister >= 8U || readLength > 8U - firstRegister) {
    owner.invalidCallback = true;
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 5,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  for (std::size_t i = 0U; i < readLength; ++i) {
    read[i] = owner.registers[firstRegister + i];
  }
  return PCA9555::TransportResult::Ok(writeLength, readLength);
}

bool pollWithOwnerBudget(PCA9555::PCA9555& device, OwnerTransport& owner,
                         uint32_t requestId, uint32_t nowMs,
                         uint8_t expectedCallbacks,
                         PCA9555::Status& terminalStatus) {
  for (uint8_t step = 0U; step < expectedCallbacks; ++step) {
    const uint32_t before = owner.callbackCount;
    uint8_t used = 0U;
    terminalStatus = device.pollOperation(requestId, nowMs + step, 1U, used);
    if (used != 1U || owner.callbackCount - before != 1U) return false;
    if (step + 1U < expectedCallbacks && !terminalStatus.inProgress()) {
      return false;
    }
  }
  return !terminalStatus.inProgress();
}

bool consumedSuccessfulResult(PCA9555::PCA9555& device, uint32_t requestId,
                              PCA9555::OperationKind kind,
                              uint8_t transactions,
                              PCA9555::OperationResult& result) {
  return device.takeOperationResult(requestId, result).ok() &&
         result.requestId == requestId && result.kind == kind &&
         result.outcome == PCA9555::OperationOutcome::SUCCEEDED &&
         result.transactionsUsed == transactions &&
         device.takeOperationResult(requestId, result)
             .is(PCA9555::Err::NO_RESULT);
}

}  // namespace

int main() {
  OwnerTransport owner{};
  PCA9555::Config config{};
  config.i2cWrite = &writeI2c;
  config.i2cWriteRead = &writeReadI2c;
  config.i2cUser = &owner;
  config.i2cAddress = DEVICE_ADDRESS;
  config.i2cTimeoutMs = OWNER_CALLBACK_TIMEOUT_MS;

  PCA9555::PCA9555 device{};
  if (!device.bind(config).ok() || owner.callbackCount != 0U) return 1;

  const PCA9555::RegisterImage safeImage{0x3CC3U, 0x0055U, 0xF00FU};
  constexpr uint32_t APPLY_ID = 0xF1610001U;
  const uint32_t beforeApply = owner.callbackCount;
  if (!device.startApplyImage(APPLY_ID, safeImage, 0U, 100U).inProgress() ||
      owner.callbackCount != beforeApply) {
    return 2;
  }
  PCA9555::Status terminal{};
  if (!pollWithOwnerBudget(device, owner, APPLY_ID, 0U,
                           PCA9555::MAX_APPLY_IMAGE_TRANSACTIONS, terminal) ||
      !terminal.ok()) {
    return 3;
  }
  PCA9555::OperationResult result{};
  if (!consumedSuccessfulResult(device, APPLY_ID,
                                PCA9555::OperationKind::APPLY_IMAGE,
                                PCA9555::MAX_APPLY_IMAGE_TRANSACTIONS,
                                result) ||
      result.observed.registers.outputs != safeImage.outputs ||
      result.observed.registers.polarity != safeImage.polarity ||
      result.observed.registers.directions != safeImage.directions) {
    return 4;
  }

  constexpr uint32_t VERIFY_ID = 0xF1610002U;
  if (!device.startVerifyImage(VERIFY_ID, safeImage, 20U, 100U).inProgress() ||
      !pollWithOwnerBudget(device, owner, VERIFY_ID, 20U,
                           PCA9555::MAX_VERIFY_IMAGE_TRANSACTIONS, terminal) ||
      !terminal.ok() ||
      !consumedSuccessfulResult(device, VERIFY_ID,
                                PCA9555::OperationKind::VERIFY_IMAGE,
                                PCA9555::MAX_VERIFY_IMAGE_TRANSACTIONS,
                                result)) {
    return 5;
  }

  constexpr uint32_t READ_ID = 0xF1610003U;
  if (!device.startReadInputs(READ_ID, 30U, 100U).inProgress() ||
      !pollWithOwnerBudget(device, owner, READ_ID, 30U,
                           PCA9555::MAX_READ_INPUTS_TRANSACTIONS, terminal) ||
      !terminal.ok() ||
      !consumedSuccessfulResult(device, READ_ID,
                                PCA9555::OperationKind::READ_INPUTS,
                                PCA9555::MAX_READ_INPUTS_TRANSACTIONS,
                                result) ||
      !result.observed.valid(PCA9555::PAIR_INPUTS) ||
      result.observed.inputs != 0xA55AU || !result.cleanupAttempted ||
      result.cleanupRequired) {
    return 6;
  }

  constexpr uint32_t FAIL_ID = 0xF1610004U;
  owner.failNextRead = true;
  if (!device.startVerifyImage(FAIL_ID, safeImage, 40U, 100U).inProgress()) {
    return 7;
  }
  const uint32_t beforeFailure = owner.callbackCount;
  uint8_t used = 0U;
  const PCA9555::Status failed =
      device.pollOperation(FAIL_ID, 40U, 1U, used);
  if (!failed.is(PCA9555::Err::I2C_NACK_ADDR) || used != 1U ||
      owner.callbackCount - beforeFailure != 1U ||
      !device.takeOperationResult(FAIL_ID, result).ok() ||
      result.outcome != PCA9555::OperationOutcome::FAILED ||
      result.transactionsUsed != 1U ||
      device.state() != PCA9555::DriverState::DEGRADED) {
    return 8;
  }

  constexpr uint32_t RECOVERY_ID = 0xF1610005U;
  if (!device.startVerifyImage(RECOVERY_ID, safeImage, 50U, 100U).inProgress() ||
      !pollWithOwnerBudget(device, owner, RECOVERY_ID, 50U,
                           PCA9555::MAX_VERIFY_IMAGE_TRANSACTIONS, terminal) ||
      !terminal.ok() || device.state() != PCA9555::DriverState::READY ||
      !consumedSuccessfulResult(device, RECOVERY_ID,
                                PCA9555::OperationKind::VERIFY_IMAGE,
                                PCA9555::MAX_VERIFY_IMAGE_TRANSACTIONS,
                                result)) {
    return 9;
  }

  const uint32_t beforeEnd = owner.callbackCount;
  if (!device.end().ok() || owner.callbackCount != beforeEnd ||
      owner.invalidCallback) {
    return 10;
  }
  return 0;
}

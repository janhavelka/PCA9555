#include <cstddef>
#include <cstdint>

#include <PCA9555/PCA9555.h>

namespace {

PCA9555::TransportResult writeI2c(uint8_t address,
                                  const uint8_t* data,
                                  std::size_t length,
                                  uint32_t timeoutMs,
                                  void*) {
  if (address != 0x20U || data == nullptr || length == 0U || timeoutMs == 0U) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  return PCA9555::TransportResult::Ok(length, 0U);
}

PCA9555::TransportResult writeReadI2c(uint8_t address,
                                      const uint8_t* write,
                                      std::size_t writeLength,
                                      uint8_t* read,
                                      std::size_t readLength,
                                      uint32_t timeoutMs,
                                      void*) {
  if (address != 0x20U || write == nullptr || writeLength == 0U ||
      read == nullptr || readLength == 0U || timeoutMs == 0U) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  for (std::size_t i = 0; i < readLength; ++i) {
    read[i] = 0xFFU;
  }
  return PCA9555::TransportResult::Ok(writeLength, readLength);
}

}  // namespace

int main() {
  PCA9555::Config config{};
  config.i2cWrite = &writeI2c;
  config.i2cWriteRead = &writeReadI2c;

  PCA9555::PCA9555 device{};
  const PCA9555::Status bound = device.bind(config);
  if (!bound.ok()) return 1;

  constexpr uint32_t REQUEST_ID = 17U;
  if (!device.startReadInputs(REQUEST_ID, 0U, 100U).inProgress()) return 2;

  uint8_t used = 0U;
  if (!device.pollOperation(REQUEST_ID, 0U, 1U, used).inProgress() ||
      used != 1U) {
    return 3;
  }
  if (!device.pollOperation(REQUEST_ID, 1U, 1U, used).ok() || used != 1U) {
    return 4;
  }

  PCA9555::OperationResult result{};
  if (!device.takeOperationResult(REQUEST_ID, result).ok() ||
      result.requestId != REQUEST_ID ||
      result.kind != PCA9555::OperationKind::READ_INPUTS ||
      result.outcome != PCA9555::OperationOutcome::SUCCEEDED ||
      result.transactionsUsed != PCA9555::MAX_READ_INPUTS_TRANSACTIONS ||
      !result.observed.valid(PCA9555::PAIR_INPUTS) ||
      result.observed.inputs != 0xFFFFU || !result.cleanupAttempted ||
      result.cleanupRequired) {
    return 5;
  }
  if (!device.takeOperationResult(REQUEST_ID, result)
           .is(PCA9555::Err::NO_RESULT)) {
    return 6;
  }

  return device.end().ok() ? 0 : 7;
}

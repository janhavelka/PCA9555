#include <cstddef>
#include <cstdint>

#include <PCA9555/PCA9555.h>

namespace {

PCA9555::Status writeI2c(uint8_t address,
                         const uint8_t* data,
                         std::size_t length,
                         uint32_t timeoutMs,
                         void*) {
  if (address != 0x20U || data == nullptr || length == 0U || timeoutMs == 0U) {
    return PCA9555::Status::Error(PCA9555::Err::I2C_ERROR,
                                 "Invalid consumer write");
  }
  return PCA9555::Status::Ok();
}

PCA9555::Status writeReadI2c(uint8_t address,
                             const uint8_t* write,
                             std::size_t writeLength,
                             uint8_t* read,
                             std::size_t readLength,
                             uint32_t timeoutMs,
                             void*) {
  if (address != 0x20U || write == nullptr || writeLength == 0U ||
      read == nullptr || readLength == 0U || timeoutMs == 0U) {
    return PCA9555::Status::Error(PCA9555::Err::I2C_ERROR,
                                 "Invalid consumer read");
  }
  for (std::size_t i = 0; i < readLength; ++i) {
    read[i] = 0xFFU;
  }
  return PCA9555::Status::Ok();
}

}  // namespace

int main() {
  PCA9555::Config config{};
  config.i2cWrite = &writeI2c;
  config.i2cWriteRead = &writeReadI2c;

  PCA9555::PCA9555 device{};
  const PCA9555::Status status = device.begin(config);
  device.end();
  return status.ok() ? 0 : 1;
}

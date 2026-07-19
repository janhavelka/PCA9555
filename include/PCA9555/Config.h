/// @file Config.h
/// @brief Injected transport and passive binding configuration.
#pragma once

#include <cstddef>
#include <cstdint>

#include "PCA9555/Status.h"

namespace PCA9555 {

/// Terminal result of exactly one physical I2C attempt.
enum class TransportCode : uint8_t {
  OK = 0,
  NACK_ADDRESS,
  NACK_DATA,
  TIMEOUT,
  BUS_ERROR,
  IO_ERROR
};

/// Conservative evidence about a write transaction's device-register effect.
enum class WriteEffect : uint8_t {
  NOT_APPLICABLE = 0,
  NOT_ATTEMPTED,          ///< Transport proves no register data was accepted.
  COMMITTED,              ///< The complete write was accepted.
  MAY_HAVE_COMMITTED      ///< Some or all register data may have been accepted.
};

/// Typed terminal outcome returned by an injected transport callback.
struct TransportResult {
  TransportCode code = TransportCode::IO_ERROR;
  int32_t detail = 0;
  WriteEffect writeEffect = WriteEffect::MAY_HAVE_COMMITTED;
  size_t completedTxBytes = 0;
  size_t completedRxBytes = 0;

  constexpr bool ok() const { return code == TransportCode::OK; }

  static constexpr TransportResult Ok(size_t txBytes, size_t rxBytes) {
    return TransportResult{TransportCode::OK, 0, WriteEffect::NOT_APPLICABLE,
                           txBytes, rxBytes};
  }

  static constexpr TransportResult Error(
      TransportCode error, int32_t detailCode = 0,
      WriteEffect effect = WriteEffect::MAY_HAVE_COMMITTED,
      size_t txBytes = 0, size_t rxBytes = 0) {
    return TransportResult{error, detailCode, effect, txBytes, rxBytes};
  }
};

/// One terminal, synchronous I2C write attempt. The callback must not retry,
/// recover the bus, retain pointers, or call the same driver recursively.
using I2cWriteFn = TransportResult (*)(uint8_t addr, const uint8_t* data,
                                       size_t len, uint32_t timeoutMs,
                                       void* user);

/// One terminal, synchronous write-then-read attempt using a repeated START.
using I2cWriteReadFn = TransportResult (*)(
    uint8_t addr, const uint8_t* txData, size_t txLen, uint8_t* rxData,
    size_t rxLen, uint32_t timeoutMs, void* user);

/// Optional monotonic uint32_t millisecond source for synchronous health stamps.
using NowMsFn = uint32_t (*)(void* user);

static constexpr uint32_t MIN_I2C_TIMEOUT_MS = 1UL;
static constexpr uint32_t MAX_I2C_TIMEOUT_MS = 1000UL;
static constexpr uint32_t DEFAULT_I2C_TIMEOUT_MS = 50UL;

/// Passive driver binding. Context pointers and callbacks must remain valid
/// until detach()/end() or a later successful bind().
struct Config {
  I2cWriteFn i2cWrite = nullptr;
  I2cWriteReadFn i2cWriteRead = nullptr;
  void* i2cUser = nullptr;

  NowMsFn nowMs = nullptr;
  void* timeUser = nullptr;

  uint8_t i2cAddress = 0x20;
  uint32_t i2cTimeoutMs = DEFAULT_I2C_TIMEOUT_MS;
};

}  // namespace PCA9555

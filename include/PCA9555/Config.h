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

/// Conservative evidence about a callback's device-side transmit phase.
enum class WriteEffect : uint8_t {
  NOT_APPLICABLE = 0,
  NOT_ATTEMPTED,      ///< Transport proves no relevant TX byte was accepted.
  COMMITTED,          ///< The complete relevant TX phase was accepted.
  MAY_HAVE_COMMITTED  ///< Some or all relevant TX bytes may have been accepted.
};

/// Typed terminal outcome returned by an injected transport callback.
struct TransportResult {
  /// Framework-neutral terminal transport classification.
  TransportCode code = TransportCode::IO_ERROR;
  /// Adapter-defined static numeric detail for diagnostics.
  int32_t detail = 0;
  /// For i2cWrite, describes register-data mutation after the leading command.
  /// For i2cWriteRead, describes acceptance of the command write phase.
  WriteEffect writeEffect = WriteEffect::MAY_HAVE_COMMITTED;
  /// TX bytes known accepted at the device boundary, including the command.
  size_t completedTxBytes = 0;
  /// RX bytes known returned by the device.
  size_t completedRxBytes = 0;

  /// Return true only for a complete successful transport attempt.
  constexpr bool ok() const { return code == TransportCode::OK; }

  /// Construct a successful result with exact completed byte counts.
  static constexpr TransportResult Ok(size_t txBytes, size_t rxBytes) {
    return TransportResult{
        TransportCode::OK, 0,
        txBytes == 0U ? WriteEffect::NOT_APPLICABLE : WriteEffect::COMMITTED,
        txBytes, rxBytes};
  }

  /// Construct a failed result with conservative transfer evidence.
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
/// writeEffect describes whether the device accepted the command write phase.
using I2cWriteReadFn = TransportResult (*)(
    uint8_t addr, const uint8_t* txData, size_t txLen, uint8_t* rxData,
    size_t rxLen, uint32_t timeoutMs, void* user);

/// Optional monotonic uint32_t millisecond source for synchronous health stamps.
using NowMsFn = uint32_t (*)(void* user);

/// Minimum accepted per-callback timeout.
static constexpr uint32_t MIN_I2C_TIMEOUT_MS = 1UL;
/// Maximum accepted per-callback timeout.
static constexpr uint32_t MAX_I2C_TIMEOUT_MS = 1000UL;
/// Default per-callback timeout used by Config.
static constexpr uint32_t DEFAULT_I2C_TIMEOUT_MS = 50UL;

/// Passive driver binding. Context pointers and callbacks must remain valid
/// until detach()/end() or a later successful bind().
struct Config {
  /// Required one-attempt write callback.
  I2cWriteFn i2cWrite = nullptr;
  /// Required one-attempt repeated-start write-read callback.
  I2cWriteReadFn i2cWriteRead = nullptr;
  /// Opaque context forwarded to both I2C callbacks.
  void* i2cUser = nullptr;

  /// Optional monotonic time callback used only for health timestamps.
  NowMsFn nowMs = nullptr;
  /// Opaque context forwarded to nowMs.
  void* timeUser = nullptr;

  /// PCA9555 7-bit address in the inclusive range 0x20 through 0x27.
  uint8_t i2cAddress = 0x20;
  /// Per-callback timeout in milliseconds.
  uint32_t i2cTimeoutMs = DEFAULT_I2C_TIMEOUT_MS;
};

}  // namespace PCA9555

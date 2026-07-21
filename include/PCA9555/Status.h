/// @file Status.h
/// @brief Error and status contracts for the PCA9555 driver.
#pragma once

#include <cstdint>

namespace PCA9555 {

/// Error codes returned by fallible driver operations.
enum class Err : uint8_t {
  OK = 0,
  NOT_INITIALIZED = 1,
  INVALID_CONFIG = 2,
  I2C_ERROR = 3,
  TIMEOUT = 4,       ///< Whole-operation deadline expired.
  INVALID_PARAM = 5,
  DEVICE_NOT_FOUND = 6,
  CONFIG_REG_MISMATCH = 7,
  BUSY = 8,
  IN_PROGRESS = 9,
  I2C_NACK_ADDR = 10,
  I2C_NACK_DATA = 11,
  I2C_TIMEOUT = 12,  ///< One transport callback timed out.
  I2C_BUS = 13,
  OFFLINE = 14,      ///< Reserved v2 compatibility value; v3 never gates I/O.
  VERIFY_MISMATCH = 15,
  NO_RESULT = 16,
  CANCELLED = 17,
  STATE_UNCERTAIN = 18,
  SHADOW_INVALID = 19,
  UNSUPPORTED = 20
};

/// Stable static name for diagnostics outside the no-logging core.
constexpr const char* errorName(Err error) {
  switch (error) {
    case Err::OK: return "OK";
    case Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case Err::I2C_ERROR: return "I2C_ERROR";
    case Err::TIMEOUT: return "TIMEOUT";
    case Err::INVALID_PARAM: return "INVALID_PARAM";
    case Err::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
    case Err::CONFIG_REG_MISMATCH: return "CONFIG_REG_MISMATCH";
    case Err::BUSY: return "BUSY";
    case Err::IN_PROGRESS: return "IN_PROGRESS";
    case Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
    case Err::I2C_NACK_DATA: return "I2C_NACK_DATA";
    case Err::I2C_TIMEOUT: return "I2C_TIMEOUT";
    case Err::I2C_BUS: return "I2C_BUS";
    case Err::OFFLINE: return "OFFLINE_RESERVED";
    case Err::VERIFY_MISMATCH: return "VERIFY_MISMATCH";
    case Err::NO_RESULT: return "NO_RESULT";
    case Err::CANCELLED: return "CANCELLED";
    case Err::STATE_UNCERTAIN: return "STATE_UNCERTAIN";
    case Err::SHADOW_INVALID: return "SHADOW_INVALID";
    case Err::UNSUPPORTED: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

/// Stable Status::detail values used with Err::BUSY.
enum class BusyDetail : int32_t {
  OPERATION_ACTIVE = 1,
  RESULT_PENDING = 2,
  REQUEST_ID_MISMATCH = 3
};

/// Status returned by all fallible public operations.
struct Status {
  /// Stable error classification.
  Err code = Err::OK;
  /// Error-specific numeric evidence; interpretation depends on code.
  int32_t detail = 0;
  const char* msg = "";  ///< Static storage only.

  /// Construct the default non-error status.
  constexpr Status() = default;
  /// Construct a status from explicit static evidence.
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}

  /// Return true only when code is Err::OK.
  constexpr bool ok() const { return code == Err::OK; }
  /// Test for one exact error code.
  constexpr bool is(Err err) const { return code == err; }
  /// Return true when cooperative work remains active.
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }
  /// Explicit boolean conversion equivalent to ok().
  constexpr explicit operator bool() const { return ok(); }

  /// Construct the canonical success status.
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }
  /// Construct a failure status with a static message and optional detail.
  static constexpr Status Error(Err err, const char* message,
                                int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

}  // namespace PCA9555

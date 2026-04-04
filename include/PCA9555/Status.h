/// @file Status.h
/// @brief Error codes and status handling for PCA9555 driver
#pragma once

#include <cstdint>

namespace PCA9555 {

/// Error codes for all PCA9555 operations
enum class Err : uint8_t {
  OK = 0,                    ///< Operation successful
  NOT_INITIALIZED,           ///< begin() not called
  INVALID_CONFIG,            ///< Invalid configuration parameter
  I2C_ERROR,                 ///< I2C communication failure
  TIMEOUT,                   ///< Operation timed out
  INVALID_PARAM,             ///< Invalid parameter value
  DEVICE_NOT_FOUND,          ///< Device not responding on I2C bus
  CONFIG_REG_MISMATCH,       ///< Configuration register != expected default
  BUSY,                      ///< Device is busy
  IN_PROGRESS,               ///< Operation scheduled; call tick() to complete

  // I2C transport details (append-only to preserve existing values)
  I2C_NACK_ADDR,             ///< I2C address not acknowledged
  I2C_NACK_DATA,             ///< I2C data byte not acknowledged
  I2C_TIMEOUT,               ///< I2C transaction timeout
  I2C_BUS                    ///< I2C bus error (arbitration lost, etc.)
};

/// Status structure returned by all fallible operations
struct Status {
  Err code = Err::OK;        ///< Error code (OK on success)
  int32_t detail = 0;        ///< Implementation-specific detail (e.g., I2C error code)
  const char* msg = "";      ///< Static string describing the error

  /// Default constructor (OK status).
  constexpr Status() = default;

  /// Construct with explicit fields.
  /// @param c   Error code
  /// @param d   Detail code
  /// @param m   Static message string
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}
  
  /// @return true if operation succeeded
  constexpr bool ok() const { return code == Err::OK; }

  /// @return true if operation is in progress
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }

  /// Create a success status.
  /// @return Status with Err::OK
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }
  
  /// Create an error status.
  /// @param err        Error code
  /// @param message    Static string describing the error
  /// @param detailCode Optional implementation-specific detail (default 0)
  /// @return Status with the given error code
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

} // namespace PCA9555

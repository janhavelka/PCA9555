/**
 * @file HealthDiag.h
 * @brief Driver health snapshot printer for interactive examples.
 *
 * NOT part of the library API. This is an example-only helper.
 */

#pragma once

#include <Arduino.h>

#include "examples/common/Log.h"
#include "PCA9555/PCA9555.h"

namespace health_diag {

inline const char* stateToStr(PCA9555::DriverState state) {
  switch (state) {
    case PCA9555::DriverState::UNINIT:
      return "UNINIT";
    case PCA9555::DriverState::READY:
      return "READY";
    case PCA9555::DriverState::DEGRADED:
      return "DEGRADED";
    case PCA9555::DriverState::OFFLINE:
      return "OFFLINE";
    default:
      return "UNKNOWN";
  }
}

inline const char* errToStr(PCA9555::Err err) {
  switch (err) {
    case PCA9555::Err::OK:
      return "OK";
    case PCA9555::Err::NOT_INITIALIZED:
      return "NOT_INITIALIZED";
    case PCA9555::Err::INVALID_CONFIG:
      return "INVALID_CONFIG";
    case PCA9555::Err::I2C_ERROR:
      return "I2C_ERROR";
    case PCA9555::Err::TIMEOUT:
      return "TIMEOUT";
    case PCA9555::Err::INVALID_PARAM:
      return "INVALID_PARAM";
    case PCA9555::Err::DEVICE_NOT_FOUND:
      return "DEVICE_NOT_FOUND";
    case PCA9555::Err::CONFIG_REG_MISMATCH:
      return "CONFIG_REG_MISMATCH";
    case PCA9555::Err::BUSY:
      return "BUSY";
    case PCA9555::Err::IN_PROGRESS:
      return "IN_PROGRESS";
    case PCA9555::Err::I2C_NACK_ADDR:
      return "I2C_NACK_ADDR";
    case PCA9555::Err::I2C_NACK_DATA:
      return "I2C_NACK_DATA";
    case PCA9555::Err::I2C_TIMEOUT:
      return "I2C_TIMEOUT";
    case PCA9555::Err::I2C_BUS:
      return "I2C_BUS";
    default:
      return "UNKNOWN";
  }
}

inline void printHealthDiag(const PCA9555::SettingsSnapshot& snapshot, uint32_t nowMs) {
  const bool online = snapshot.state == PCA9555::DriverState::READY ||
                      snapshot.state == PCA9555::DriverState::DEGRADED;
  const uint64_t total = static_cast<uint64_t>(snapshot.totalSuccess) +
                         static_cast<uint64_t>(snapshot.totalFailures);
  const float successRate = (total > 0U)
                                ? (100.0f * static_cast<float>(snapshot.totalSuccess) /
                                   static_cast<float>(total))
                                : 0.0f;
  Serial.println("=== Driver Health ===");
  Serial.printf("  State: %s\n", stateToStr(snapshot.state));
  Serial.printf("  Online: %s\n", log_bool_str(online));
  Serial.printf("  Consecutive failures: %u\n", snapshot.consecutiveFailures);
  Serial.printf("  Total success: %lu\n", static_cast<unsigned long>(snapshot.totalSuccess));
  Serial.printf("  Total failures: %lu\n", static_cast<unsigned long>(snapshot.totalFailures));
  Serial.printf("  Success rate: %.1f%%\n", successRate);

  if (snapshot.lastOkMs > 0U) {
    Serial.printf("  Last OK: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(nowMs - snapshot.lastOkMs),
                  static_cast<unsigned long>(snapshot.lastOkMs));
  } else {
    Serial.println("  Last OK: never");
  }

  if (snapshot.lastErrorMs > 0U) {
    Serial.printf("  Last error: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(nowMs - snapshot.lastErrorMs),
                  static_cast<unsigned long>(snapshot.lastErrorMs));
  } else {
    Serial.println("  Last error: never");
  }

  if (!snapshot.lastError.ok()) {
    Serial.printf("  Error code: %s\n", errToStr(snapshot.lastError.code));
    Serial.printf("  Error detail: %ld\n", static_cast<long>(snapshot.lastError.detail));
    if (snapshot.lastError.msg && snapshot.lastError.msg[0] != '\0') {
      Serial.printf("  Error msg: %s\n", snapshot.lastError.msg);
    }
  }
  Serial.flush();
}

}  // namespace health_diag

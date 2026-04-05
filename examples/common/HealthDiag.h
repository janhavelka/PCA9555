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
  const char* stateColor =
      (snapshot.state == PCA9555::DriverState::UNINIT)
          ? LOG_COLOR_RESET
          : LOG_COLOR_STATE(online, snapshot.consecutiveFailures);

  Serial.println("=== Driver Health ===");
  Serial.printf("  State: %s%s%s\n",
                stateColor,
                stateToStr(snapshot.state),
                LOG_COLOR_RESET);
  Serial.printf("  Online: %s%s%s\n",
                online ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                log_bool_str(online),
                LOG_COLOR_RESET);
  Serial.printf("  Consecutive failures: %s%u%s\n",
                (snapshot.consecutiveFailures == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                snapshot.consecutiveFailures,
                LOG_COLOR_RESET);
  Serial.printf("  Total success: %s%lu%s\n",
                (snapshot.totalSuccess > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW,
                static_cast<unsigned long>(snapshot.totalSuccess),
                LOG_COLOR_RESET);
  Serial.printf("  Total failures: %s%lu%s\n",
                (snapshot.totalFailures == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                static_cast<unsigned long>(snapshot.totalFailures),
                LOG_COLOR_RESET);
  Serial.printf("  Success rate: %s%.1f%%%s\n",
                (successRate >= 99.9f) ? LOG_COLOR_GREEN
                                       : ((successRate >= 80.0f) ? LOG_COLOR_YELLOW
                                                                 : LOG_COLOR_RED),
                successRate,
                LOG_COLOR_RESET);

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
    Serial.printf("  Error code: %s%s%s\n",
                  LOG_COLOR_RED,
                  errToStr(snapshot.lastError.code),
                  LOG_COLOR_RESET);
    Serial.printf("  Error detail: %ld\n", static_cast<long>(snapshot.lastError.detail));
    if (snapshot.lastError.msg && snapshot.lastError.msg[0] != '\0') {
      Serial.printf("  Error msg: %s\n", snapshot.lastError.msg);
    }
  }
}

}  // namespace health_diag

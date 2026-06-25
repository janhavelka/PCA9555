/**
 * @file HealthDiag.h
 * @brief Driver health snapshot printer for interactive examples.
 *
 * NOT part of the library API. This is an example-only helper.
 */

#pragma once

#include <Arduino.h>

#include <cstdio>

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
  (void)nowMs;
  const char* lastErrorText = (snapshot.lastErrorMs == 0U) ? "never"
                                                           : errToStr(snapshot.lastError.code);
  Serial.println("=== Driver Health ===");
  char line[192];
  std::snprintf(line, sizeof(line),
                "  State: %s Online: %s Consecutive failures: %u Total success: %lu "
                "Total failures: %lu Last error: %s",
                stateToStr(snapshot.state),
                log_bool_str(online),
                snapshot.consecutiveFailures,
                static_cast<unsigned long>(snapshot.totalSuccess),
                static_cast<unsigned long>(snapshot.totalFailures),
                lastErrorText);
  Serial.println(line);

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

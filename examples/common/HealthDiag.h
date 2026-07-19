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
    default:
      return "UNKNOWN";
  }
}

inline const char* errToStr(PCA9555::Err err) {
  return PCA9555::errorName(err);
}

inline void printHealthDiag(const PCA9555::SettingsSnapshot& snapshot, uint32_t nowMs) {
  (void)nowMs;
  const char* lastErrorText = (snapshot.lastErrorMs == 0U) ? "never"
                                                           : errToStr(snapshot.lastError.code);
  Serial.println("=== Driver Health ===");
  char line[192];
  std::snprintf(line, sizeof(line),
                "  State: %s Bound: %s Consecutive failures: %u Total success: %lu "
                "Total failures: %lu Last error: %s",
                stateToStr(snapshot.state),
                log_bool_str(snapshot.initialized),
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

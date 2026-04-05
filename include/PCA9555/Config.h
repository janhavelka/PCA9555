/// @file Config.h
/// @brief Configuration structure for PCA9555 driver
#pragma once

#include <cstddef>
#include <cstdint>
#include "PCA9555/Status.h"

namespace PCA9555 {

/// I2C write callback signature
/// @param addr     I2C device address (7-bit)
/// @param data     Pointer to data to write
/// @param len      Number of bytes to write
/// @param timeoutMs Maximum time to wait for completion
/// @param user     User context pointer passed through from Config
/// @return Status indicating success or failure
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C write-then-read callback signature
/// @param addr     I2C device address (7-bit)
/// @param txData   Pointer to data to write
/// @param txLen    Number of bytes to write
/// @param rxData   Pointer to buffer for read data
/// @param rxLen    Number of bytes to read
/// @param timeoutMs Maximum time to wait for completion
/// @param user     User context pointer passed through from Config
/// @return Status indicating success or failure
using I2cWriteReadFn = Status (*)(uint8_t addr, const uint8_t* txData, size_t txLen,
                                  uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                  void* user);

/// Millisecond timestamp callback.
/// @param user User context pointer passed through from Config
/// @return Current monotonic milliseconds
using NowMsFn = uint32_t (*)(void* user);

/// Port identifier
enum class Port : uint8_t {
  PORT_0 = 0,  ///< Port 0 (P00–P07)
  PORT_1 = 1   ///< Port 1 (P10–P17)
};

/// Pin number (0–15 across both ports)
/// Pins 0–7 = Port 0, Pins 8–15 = Port 1
using Pin = uint8_t;

/// Configuration for PCA9555 driver
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;        ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C write-read function pointer
  void* i2cUser = nullptr;               ///< User context for callbacks

  // === Timing Hooks (optional) ===
  NowMsFn nowMs = nullptr;               ///< Monotonic millisecond source
  void* timeUser = nullptr;              ///< User context for timing hook
  
  // === Device Settings ===
  uint8_t i2cAddress = 0x20;             ///< 0x20–0x27 (A2:A1:A0 pin state)
  uint32_t i2cTimeoutMs = 50;            ///< I2C transaction timeout in ms

  // === Initial Pin Configuration ===
  uint8_t configPort0 = 0xFF;            ///< Pin direction Port 0 (1=input, 0=output). Default: all inputs
  uint8_t configPort1 = 0xFF;            ///< Pin direction Port 1 (1=input, 0=output). Default: all inputs
  uint8_t outputPort0 = 0xFF;            ///< Initial output value Port 0. Default: all high
  uint8_t outputPort1 = 0xFF;            ///< Initial output value Port 1. Default: all high
  uint8_t polarityPort0 = 0x00;          ///< Polarity inversion Port 0. Default: no inversion
  uint8_t polarityPort1 = 0x00;          ///< Polarity inversion Port 1. Default: no inversion
  bool requireConfigPortDefaults = true; ///< Require Configuration Port 0/1 = 0xFF at begin()

  // === Interrupt Errata Workaround ===
  bool applyInterruptErrata = true;      ///< Write safe cmd byte after input reads (recommended)

  // === Health Tracking ===
  uint8_t offlineThreshold = 5;          ///< Consecutive failures before OFFLINE state (1–255)
};

} // namespace PCA9555

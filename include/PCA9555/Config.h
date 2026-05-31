/// @file Config.h
/// @brief Configuration structure for PCA9555 driver
#pragma once

#include <cstddef>
#include <cstdint>
#include "PCA9555/Status.h"

namespace PCA9555 {

/// I2C write callback signature.
///
/// Callback contract: complete synchronously, do not retain buffer pointers,
/// and do not call back into the same PCA9555 instance. If Config::i2cLock is
/// configured, callbacks invoked by this driver while that lock is held must
/// not attempt to acquire the same non-recursive lock again.
/// @param addr     I2C device address (7-bit)
/// @param data     Pointer to data to write
/// @param len      Number of bytes to write
/// @param timeoutMs Maximum time to wait for completion
/// @param user     User context pointer passed through from Config
/// @return Status indicating success or failure
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C write-then-read callback signature.
///
/// Callback contract: complete synchronously, do not retain buffer pointers,
/// and do not call back into the same PCA9555 instance. If Config::i2cLock is
/// configured, callbacks invoked by this driver while that lock is held must
/// not attempt to acquire the same non-recursive lock again.
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

/// Optional transport lock callback.
///
/// If configured, the driver calls this before compound I2C sequences that must
/// not be interleaved by another shared-bus user. The callback must complete
/// synchronously and must not call back into the same PCA9555 instance.
/// @param user User context pointer passed through from Config
/// @param timeoutMs Maximum time to wait for the lock
/// @return Status::Ok() after acquiring the lock, error otherwise
using LockFn = Status (*)(void* user, uint32_t timeoutMs);

/// Optional transport unlock callback.
///
/// Called exactly once for each successful LockFn call, including error return
/// paths after the lock was acquired.
/// @param user User context pointer passed through from Config
using UnlockFn = void (*)(void* user);

/// Millisecond timestamp callback.
///
/// Optional diagnostic timebase. If absent, health timestamps remain 0.
/// The timebase must be monotonic uint32_t milliseconds; wrap is allowed.
/// Use the same clock domain as tick(nowMs).
/// @param user User context pointer passed through from Config
/// @return Current monotonic milliseconds
using NowMsFn = uint32_t (*)(void* user);

/// @brief Port identifier.
enum class Port : uint8_t {
  PORT_0 = 0,  ///< Port 0 (P00-P07)
  PORT_1 = 1   ///< Port 1 (P10-P17)
};

/// @brief Pin direction.
enum class Direction : uint8_t {
  INPUT_MODE = 0,   ///< High-Z input
  OUTPUT_MODE = 1   ///< Push-pull output driven by the output latch
};

/// Pin number (0-15 across both ports)
/// Pins 0-7 = Port 0, Pins 8-15 = Port 1
using Pin = uint8_t;

/// @brief Configuration for PCA9555 driver.
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;         ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C write-read function pointer
  void* i2cUser = nullptr;               ///< User context for callbacks

  // === Timing Hooks (optional) ===
  NowMsFn nowMs = nullptr;               ///< Optional diagnostic timestamp source
  void* timeUser = nullptr;              ///< User context for timing hook

  // === Device Settings ===
  uint8_t i2cAddress = 0x20;             ///< 0x20-0x27 (A2:A1:A0 pin state)
  uint32_t i2cTimeoutMs = 50;            ///< I2C transaction timeout in ms

  // === Initial Pin Configuration ===
  uint8_t configPort0 = 0xFF;            ///< Pin direction Port 0 (1=input, 0=output). Default: all inputs
  uint8_t configPort1 = 0xFF;            ///< Pin direction Port 1 (1=input, 0=output). Default: all inputs
  uint8_t outputPort0 = 0xFF;            ///< Initial output latch Port 0. Default: all latch bits high
  uint8_t outputPort1 = 0xFF;            ///< Initial output latch Port 1. Default: all latch bits high
  uint8_t polarityPort0 = 0x00;          ///< Polarity inversion Port 0. Default: no inversion
  uint8_t polarityPort1 = 0x00;          ///< Polarity inversion Port 1. Default: no inversion
  bool requireConfigPortDefaults = true; ///< Require Configuration Port 0/1 = 0xFF at begin()

  // === Interrupt Errata Workaround ===
  bool applyInterruptErrata = true;      ///< Write safe cmd byte after input reads (recommended)

  // === Health Tracking ===
  uint8_t offlineThreshold = 5;          ///< Consecutive failures before OFFLINE state (1-255)

  // === Optional Shared-Bus Compound Sequence Lock ===
  LockFn i2cLock = nullptr;              ///< Optional lock for input-read + errata-write sequences
  UnlockFn i2cUnlock = nullptr;          ///< Optional unlock; must be set when i2cLock is set
  void* lockUser = nullptr;              ///< User context for lock callbacks
};

} // namespace PCA9555

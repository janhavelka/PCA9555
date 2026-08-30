/**
 * @file I2cTransport.h
 * @brief Wire-based I2C transport adapter for PCA9555 examples.
 *
 * This file provides Wire-compatible I2C callbacks that can be
 * used with the PCA9555 driver. The library does not depend on Wire
 * directly; this adapter bridges them.
 *
 * NOT part of the library API. Example-only.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "PCA9555/Config.h"

namespace transport {

static constexpr uint32_t WIRE_TIMEOUT_MAX_MS = 65535U;

inline uint16_t clampWireTimeoutMs(uint32_t timeoutMs) {
  if (timeoutMs == 0U) {
    return 1U;
  }
  if (timeoutMs > WIRE_TIMEOUT_MAX_MS) {
    return static_cast<uint16_t>(WIRE_TIMEOUT_MAX_MS);
  }
  return static_cast<uint16_t>(timeoutMs);
}

class ScopedWireTimeout {
 public:
  ScopedWireTimeout(TwoWire& wire, uint32_t timeoutMs)
      : _wire(wire)
#if defined(ARDUINO_ARCH_ESP32)
      , _previous(wire.getTimeOut())
      , _changed(_previous != clampWireTimeoutMs(timeoutMs))
#endif
  {
#if defined(ARDUINO_ARCH_ESP32)
    if (_changed) {
      _wire.setTimeOut(clampWireTimeoutMs(timeoutMs));
    }
#else
    (void)timeoutMs;
#endif
  }

  ~ScopedWireTimeout() {
#if defined(ARDUINO_ARCH_ESP32)
    if (_changed) {
      _wire.setTimeOut(_previous);
    }
#endif
  }

  ScopedWireTimeout(const ScopedWireTimeout&) = delete;
  ScopedWireTimeout& operator=(const ScopedWireTimeout&) = delete;

 private:
  TwoWire& _wire;
#if defined(ARDUINO_ARCH_ESP32)
  uint16_t _previous;
  bool _changed;
#endif
};

/**
 * @brief Map a NONZERO Wire endTransmission() code to a terminal result.
 *
 * Callers must handle result == 0 themselves, because only they know the byte
 * counts a successful transfer completed. Returning Ok(0, 0) here would be
 * rejected by the driver as an incomplete successful transport.
 */
inline PCA9555::TransportResult mapWireResult(
    uint8_t result,
    PCA9555::WriteEffect writeEffect = PCA9555::WriteEffect::NOT_APPLICABLE) {
  switch (result) {
    case 1:
      return PCA9555::TransportResult::Error(
          PCA9555::TransportCode::IO_ERROR, result,
          PCA9555::WriteEffect::NOT_ATTEMPTED);
    case 2:
      return PCA9555::TransportResult::Error(
          PCA9555::TransportCode::NACK_ADDRESS, result,
          PCA9555::WriteEffect::NOT_ATTEMPTED);
    case 3:
      return PCA9555::TransportResult::Error(
          PCA9555::TransportCode::NACK_DATA, result, writeEffect);
    case 4:
      return PCA9555::TransportResult::Error(
          PCA9555::TransportCode::BUS_ERROR, result, writeEffect);
    case 5:
      return PCA9555::TransportResult::Error(
          PCA9555::TransportCode::TIMEOUT, result, writeEffect);
    default:
      return PCA9555::TransportResult::Error(
          PCA9555::TransportCode::IO_ERROR, result, writeEffect);
  }
}

/**
 * @brief Wire-based I2C write implementation.
 *
 * Pass to Config::i2cWrite, and pass &Wire (or a custom TwoWire*) to i2cUser.
 * Applies the requested timeout for the transaction and restores the previous
 * Wire timeout afterwards.
 *
 * @param addr I2C 7-bit address
 * @param data Data buffer to send
 * @param len Number of bytes
 * @param timeoutMs Per-attempt timeout requested by the driver
 * @param user Pointer to TwoWire instance
 * @return Terminal result for one physical attempt
 */
inline PCA9555::TransportResult wireWrite(uint8_t addr, const uint8_t* data,
                                          size_t len, uint32_t timeoutMs,
                                          void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  if (!data || len == 0) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }

  // Check for oversized writes (ESP32 Wire buffer is 128 bytes)
  if (len > 128) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, static_cast<int32_t>(len),
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }

  ScopedWireTimeout scopedTimeout(*wire, timeoutMs);
  wire->beginTransmission(addr);
  size_t written = wire->write(data, len);
  if (written != len) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, static_cast<int32_t>(written),
        PCA9555::WriteEffect::NOT_ATTEMPTED, 0U, 0U);
  }

  uint8_t result = wire->endTransmission(true);  // Send STOP
  if (result == 0U) {
    return PCA9555::TransportResult::Ok(len, 0U);
  }
  return mapWireResult(result, PCA9555::WriteEffect::MAY_HAVE_COMMITTED);
}

/**
 * @brief Wire-based I2C write-read implementation.
 *
 * Pass to Config::i2cWriteRead, and pass &Wire (or a custom TwoWire*) to i2cUser.
 * Applies the requested timeout for the transaction and restores the previous
 * Wire timeout afterwards.
 *
 * @param addr I2C 7-bit address
 * @param tx TX buffer to send
 * @param txLen TX length
 * @param rx RX buffer for readback
 * @param rxLen RX length
 * @param timeoutMs Per-attempt timeout requested by the driver
 * @param user Pointer to TwoWire instance
 * @return Terminal result for one physical attempt
 */
inline PCA9555::TransportResult wireWriteRead(uint8_t addr, const uint8_t* tx,
                                              size_t txLen, uint8_t* rx,
                                              size_t rxLen, uint32_t timeoutMs,
                                              void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  if ((txLen > 0 && tx == nullptr) || (rxLen > 0 && rx == nullptr)) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  if (txLen == 0 || rxLen == 0) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  if (txLen > 128 || rxLen > 128) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }

  ScopedWireTimeout scopedTimeout(*wire, timeoutMs);
  wire->beginTransmission(addr);
  size_t written = wire->write(tx, txLen);
  if (written != txLen) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, static_cast<int32_t>(written),
        PCA9555::WriteEffect::NOT_ATTEMPTED, 0U, 0U);
  }

  uint8_t result = wire->endTransmission(false);  // Repeated start
  if (result != 0) {
    const PCA9555::WriteEffect commandEffect =
        (result == 2U || result == 3U)
            ? PCA9555::WriteEffect::NOT_ATTEMPTED
            : PCA9555::WriteEffect::MAY_HAVE_COMMITTED;
    return mapWireResult(result, commandEffect);
  }

  size_t read = wire->requestFrom(addr, static_cast<uint8_t>(rxLen));
  if (read != rxLen) {
    // On Arduino-ESP32, endTransmission(false) only stages the command and
    // requestFrom() performs the combined write-read transfer. Receiving at
    // least one byte proves the command was accepted; a zero-byte result does
    // not distinguish command NACK, read failure, timeout, or bus error.
    const bool commandAccepted = read > 0U;
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, static_cast<int32_t>(read),
        commandAccepted ? PCA9555::WriteEffect::COMMITTED
                        : PCA9555::WriteEffect::MAY_HAVE_COMMITTED,
        commandAccepted ? txLen : 0U, read);
  }

  for (size_t i = 0; i < rxLen; ++i) {
    if (wire->available()) {
      rx[i] = static_cast<uint8_t>(wire->read());
    } else {
      return PCA9555::TransportResult::Error(
          PCA9555::TransportCode::IO_ERROR, 0,
          PCA9555::WriteEffect::COMMITTED, txLen, i);
    }
  }

  return PCA9555::TransportResult::Ok(txLen, rxLen);
}

/**
 * @brief Initialize Wire with default pins and frequency.
 *
 * @param sda SDA pin number
 * @param scl SCL pin number
 * @param freq I2C clock frequency in Hz (default 400kHz)
 * @param timeoutMs I2C timeout in milliseconds (default 50ms)
 * @return true on success
 */
inline bool initWire(int sda, int scl, uint32_t freq = 400000, uint16_t timeoutMs = 50) {
#if defined(ARDUINO_ARCH_ESP32)
  // Toggle SCL to release any stuck slave
  pinMode(scl, OUTPUT);
  pinMode(sda, INPUT_PULLUP);
  for (int i = 0; i < 9; i++) {
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
  }
  // Generate STOP condition
  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);
#endif

  if (!Wire.begin(sda, scl, freq)) {
    return false;
  }
  Wire.setTimeOut(timeoutMs);
  return true;
}

}  // namespace transport

/**
 * @file IdfArduinoCompat.h
 * @brief ESP-IDF compatibility layer for the Arduino-shaped PCA9555 CLI example.
 *
 * This is example glue only. It implements the small Arduino surface used by
 * examples/01_basic_bringup_cli so the ESP-IDF example can share the same
 * command implementation without linking the Arduino framework.
 */

#pragma once

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "PCA9555/Status.h"

#ifndef PCA9555_EXAMPLE_PLATFORM_IDF
#define PCA9555_EXAMPLE_PLATFORM_IDF 1
#endif

static constexpr uint8_t LOW = 0U;
static constexpr uint8_t HIGH = 1U;
static constexpr uint8_t INPUT_PULLUP = 2U;
static constexpr uint8_t OUTPUT = 1U;

inline uint32_t millis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

inline uint32_t micros() {
  return static_cast<uint32_t>(esp_timer_get_time());
}

inline TickType_t idfExampleDelayTicks(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ticks == 0 && ms > 0U) {
    ticks = 1;
  }
  return ticks;
}

inline void delay(uint32_t ms) {
  vTaskDelay(idfExampleDelayTicks(ms));
}

inline void delayMicroseconds(uint32_t us) {
  esp_rom_delay_us(us);
}

inline void yield() {
  vTaskDelay(1);
}

inline void pinMode(uint8_t pin, uint8_t mode) {
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  if (mode == OUTPUT) {
    (void)gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
  } else if (mode == INPUT_PULLUP) {
    (void)gpio_set_direction(gpio, GPIO_MODE_INPUT);
    (void)gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
  }
}

inline void digitalWrite(uint8_t pin, uint8_t value) {
  (void)gpio_set_level(static_cast<gpio_num_t>(pin), value == HIGH ? 1 : 0);
}

inline int digitalRead(uint8_t pin) {
  return gpio_get_level(static_cast<gpio_num_t>(pin));
}

class String {
 public:
  String() { clear(); }
  String(const char* text) { assign(text); }
  String(const String& other) { assign(other.c_str()); }

  String& operator=(const char* text) {
    assign(text);
    return *this;
  }

  String& operator=(const String& other) {
    if (this != &other) {
      assign(other.c_str());
    }
    return *this;
  }

  bool reserve(size_t capacity) const {
    return capacity < kCapacity;
  }

  size_t length() const {
    return _len;
  }

  const char* c_str() const {
    return _buf;
  }

  void trim() {
    size_t first = 0U;
    while (first < _len &&
           std::isspace(static_cast<unsigned char>(_buf[first])) != 0) {
      ++first;
    }
    size_t last = _len;
    while (last > first &&
           std::isspace(static_cast<unsigned char>(_buf[last - 1U])) != 0) {
      --last;
    }
    if (first > 0U) {
      const size_t newLen = last - first;
      std::memmove(_buf, _buf + first, newLen);
      _len = newLen;
      _buf[_len] = '\0';
    } else {
      _len = last;
      _buf[_len] = '\0';
    }
  }

  void remove(size_t index) {
    if (index >= _len) {
      return;
    }
    _len = index;
    _buf[_len] = '\0';
  }

  bool startsWith(const char* prefix) const {
    if (prefix == nullptr) {
      return false;
    }
    const size_t prefixLen = std::strlen(prefix);
    return prefixLen <= _len && std::memcmp(_buf, prefix, prefixLen) == 0;
  }

  String substring(size_t start) const {
    if (start >= _len) {
      return String("");
    }
    return String(_buf + start);
  }

  String& operator+=(char c) {
    if (_len < (kCapacity - 1U)) {
      _buf[_len++] = c;
      _buf[_len] = '\0';
    }
    return *this;
  }

  bool operator==(const char* rhs) const {
    return rhs != nullptr && std::strcmp(_buf, rhs) == 0;
  }

  bool operator!=(const char* rhs) const {
    return !(*this == rhs);
  }

  bool operator==(const String& rhs) const {
    return std::strcmp(_buf, rhs._buf) == 0;
  }

 private:
  static constexpr size_t kCapacity = 192U;

  void clear() {
    _buf[0] = '\0';
    _len = 0U;
  }

  void assign(const char* text) {
    if (text == nullptr) {
      clear();
      return;
    }
    std::strncpy(_buf, text, kCapacity - 1U);
    _buf[kCapacity - 1U] = '\0';
    _len = std::strlen(_buf);
  }

  char _buf[kCapacity] = {};
  size_t _len = 0U;
};

inline bool operator==(const char* lhs, const String& rhs) {
  return rhs == lhs;
}

class IdfConsole {
 public:
  void begin(unsigned long) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
      (void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
  }

  explicit operator bool() const {
    return true;
  }

  int available() {
    pollInput();
    return static_cast<int>(_count);
  }

  int read() {
    pollInput();
    if (_count == 0U) {
      return -1;
    }
    const uint8_t value = _rx[_tail];
    _tail = (_tail + 1U) % kRxCapacity;
    --_count;
    return static_cast<int>(value);
  }

  size_t write(uint8_t value) {
    return fwrite(&value, 1U, 1U, stdout);
  }

  size_t write(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0U) {
      return 0U;
    }
    return fwrite(data, 1U, len, stdout);
  }

  int printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int written = vfprintf(stdout, fmt, args);
    va_end(args);
    return written;
  }

  void print(const char* value) {
    if (value != nullptr) {
      fputs(value, stdout);
    }
  }

  void print(char value) {
    (void)write(static_cast<uint8_t>(value));
  }

  void print(int value) {
    (void)printf("%d", value);
  }

  void print(unsigned int value) {
    (void)printf("%u", value);
  }

  void print(long value) {
    (void)printf("%ld", value);
  }

  void print(unsigned long value) {
    (void)printf("%lu", value);
  }

  void println() {
    (void)write(static_cast<uint8_t>('\n'));
  }

  template <typename T>
  void println(T value) {
    print(value);
    println();
  }

  void flush() {
    fflush(stdout);
  }

 private:
  static constexpr size_t kRxCapacity = 256U;

  void pollInput() {
    while (_count < kRxCapacity) {
      uint8_t value = 0U;
      const ssize_t readCount = ::read(STDIN_FILENO, &value, 1U);
      if (readCount == 1) {
        _rx[_head] = value;
        _head = (_head + 1U) % kRxCapacity;
        ++_count;
        continue;
      }
      if (readCount < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
      }
      return;
    }
  }

  uint8_t _rx[kRxCapacity] = {};
  size_t _head = 0U;
  size_t _tail = 0U;
  size_t _count = 0U;
};

class TwoWire {
 public:
  bool begin(int sda, int scl) {
    end();

    i2c_master_bus_config_t busConfig = {};
    busConfig.i2c_port = I2C_NUM_0;
    busConfig.sda_io_num = static_cast<gpio_num_t>(sda);
    busConfig.scl_io_num = static_cast<gpio_num_t>(scl);
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;

    _lastError = i2c_new_master_bus(&busConfig, &_bus);
    _started = (_lastError == ESP_OK);
    return _started;
  }

  void end() {
    clearDevice();
    if (_bus != nullptr) {
      (void)i2c_del_master_bus(_bus);
      _bus = nullptr;
    }
    _started = false;
    _pendingRepeatedStart = false;
    _rxLen = 0U;
    _rxIndex = 0U;
  }

  void setClock(uint32_t freq) {
    _freqHz = freq;
    clearDevice();
  }

  void setTimeOut(uint16_t timeoutMs) {
    _timeoutMs = timeoutMs;
  }

  void beginTransmission(uint8_t addr) {
    _txAddr = addr;
    _txLen = 0U;
    _pendingRepeatedStart = false;
  }

  size_t write(uint8_t value) {
    return write(&value, 1U);
  }

  size_t write(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0U) {
      return 0U;
    }
    const size_t room = kBufferSize - _txLen;
    const size_t toCopy = (len < room) ? len : room;
    std::memcpy(_tx + _txLen, data, toCopy);
    _txLen += toCopy;
    return toCopy;
  }

  uint8_t endTransmission(bool sendStop = true) {
    if (!sendStop) {
      _pendingAddr = _txAddr;
      _pendingTxLen = _txLen;
      std::memcpy(_pendingTx, _tx, _pendingTxLen);
      _pendingRepeatedStart = true;
      return 0U;
    }
    if (_txLen == 0U) {
      return wireCodeFromEsp(probe(_txAddr, _timeoutMs));
    }
    return wireCodeFromEsp(transmit(_txAddr, _tx, _txLen, _timeoutMs));
  }

  size_t requestFrom(uint8_t addr, uint8_t len) {
    _rxLen = 0U;
    _rxIndex = 0U;
    const size_t requestLen = (len < kBufferSize) ? len : kBufferSize;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (_pendingRepeatedStart && _pendingAddr == addr && _pendingTxLen > 0U) {
      err = transmitReceive(addr, _pendingTx, _pendingTxLen, _rx, requestLen,
                            _timeoutMs);
    } else {
      err = receive(addr, _rx, requestLen, _timeoutMs);
    }
    _pendingRepeatedStart = false;
    if (err == ESP_OK) {
      _rxLen = requestLen;
    }
    return _rxLen;
  }

  int available() const {
    return static_cast<int>(_rxLen - _rxIndex);
  }

  int read() {
    if (_rxIndex >= _rxLen) {
      return -1;
    }
    return static_cast<int>(_rx[_rxIndex++]);
  }

  PCA9555::Status writeStatus(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs) {
    if (data == nullptr || len == 0U) {
      return PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM,
                                    "Invalid I2C write params");
    }
    return mapStatus(transmit(addr, data, len, timeoutMs), "I2C write failed");
  }

  PCA9555::Status writeReadStatus(uint8_t addr, const uint8_t* tx,
                                  size_t txLen, uint8_t* rx, size_t rxLen,
                                  uint32_t timeoutMs) {
    if ((txLen > 0U && tx == nullptr) || (rxLen > 0U && rx == nullptr) ||
        txLen == 0U || rxLen == 0U) {
      return PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM,
                                    "Invalid I2C read params");
    }
    return mapStatus(transmitReceive(addr, tx, txLen, rx, rxLen, timeoutMs),
                     "I2C write-read failed");
  }

 private:
  static constexpr size_t kBufferSize = 128U;

  static int timeoutArg(uint32_t timeoutMs) {
    if (timeoutMs > static_cast<uint32_t>(INT_MAX)) {
      return INT_MAX;
    }
    return static_cast<int>(timeoutMs);
  }

  static uint8_t wireCodeFromEsp(esp_err_t err) {
    if (err == ESP_OK) {
      return 0U;
    }
    if (err == ESP_ERR_TIMEOUT) {
      return 5U;
    }
    if (err == ESP_ERR_INVALID_ARG) {
      return 1U;
    }
    if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_FOUND) {
      return 2U;
    }
    return 4U;
  }

  static PCA9555::Status mapStatus(esp_err_t err, const char* context) {
    if (err == ESP_OK) {
      return PCA9555::Status::Ok();
    }
    if (err == ESP_ERR_TIMEOUT) {
      return PCA9555::Status::Error(PCA9555::Err::I2C_TIMEOUT, context, err);
    }
    if (err == ESP_ERR_INVALID_ARG) {
      return PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, context, err);
    }
    if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_FOUND) {
      return PCA9555::Status::Error(PCA9555::Err::I2C_BUS, context, err);
    }
    return PCA9555::Status::Error(PCA9555::Err::I2C_ERROR, context, err);
  }

  void clearDevice() {
    if (_dev != nullptr) {
      (void)i2c_master_bus_rm_device(_dev);
      _dev = nullptr;
      _devAddr = 0xFFU;
    }
  }

  esp_err_t ensureDevice(uint8_t addr) {
    if (!_started || _bus == nullptr) {
      return ESP_ERR_INVALID_STATE;
    }
    if (_dev != nullptr && _devAddr == addr) {
      return ESP_OK;
    }
    clearDevice();

    i2c_device_config_t devConfig = {};
    devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devConfig.device_address = addr;
    devConfig.scl_speed_hz = _freqHz;
    const esp_err_t err = i2c_master_bus_add_device(_bus, &devConfig, &_dev);
    if (err == ESP_OK) {
      _devAddr = addr;
    }
    return err;
  }

  esp_err_t probe(uint8_t addr, uint32_t timeoutMs) {
    if (!_started || _bus == nullptr) {
      return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(_bus, addr, timeoutArg(timeoutMs));
  }

  esp_err_t transmit(uint8_t addr, const uint8_t* data, size_t len,
                     uint32_t timeoutMs) {
    esp_err_t err = ensureDevice(addr);
    if (err != ESP_OK) {
      return err;
    }
    return i2c_master_transmit(_dev, data, len, timeoutArg(timeoutMs));
  }

  esp_err_t receive(uint8_t addr, uint8_t* data, size_t len,
                    uint32_t timeoutMs) {
    esp_err_t err = ensureDevice(addr);
    if (err != ESP_OK) {
      return err;
    }
    return i2c_master_receive(_dev, data, len, timeoutArg(timeoutMs));
  }

  esp_err_t transmitReceive(uint8_t addr, const uint8_t* tx, size_t txLen,
                            uint8_t* rx, size_t rxLen, uint32_t timeoutMs) {
    esp_err_t err = ensureDevice(addr);
    if (err != ESP_OK) {
      return err;
    }
    return i2c_master_transmit_receive(_dev, tx, txLen, rx, rxLen,
                                       timeoutArg(timeoutMs));
  }

  i2c_master_bus_handle_t _bus = nullptr;
  i2c_master_dev_handle_t _dev = nullptr;
  uint8_t _devAddr = 0xFFU;
  bool _started = false;
  uint32_t _freqHz = 400000U;
  uint16_t _timeoutMs = 50U;
  esp_err_t _lastError = ESP_OK;

  uint8_t _txAddr = 0U;
  uint8_t _tx[kBufferSize] = {};
  size_t _txLen = 0U;
  bool _pendingRepeatedStart = false;
  uint8_t _pendingAddr = 0U;
  uint8_t _pendingTx[kBufferSize] = {};
  size_t _pendingTxLen = 0U;
  uint8_t _rx[kBufferSize] = {};
  size_t _rxLen = 0U;
  size_t _rxIndex = 0U;
};

extern IdfConsole Serial;
extern TwoWire Wire;

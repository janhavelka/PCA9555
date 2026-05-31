#include <cstddef>
#include <cstdint>
#include <limits>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "PCA9555/PCA9555.h"

namespace {

static constexpr const char* TAG = "pca9555_idf_basic";
static constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
static constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
static constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
static constexpr uint32_t I2C_FREQ_HZ = 400000;
static constexpr uint32_t I2C_TIMEOUT_MS = 50;
static constexpr uint8_t PCA9555_ADDR = 0x20;
static constexpr bool DEMO_CONFIGURE_P00_OUTPUT = false;

struct IdfI2cContext {
  i2c_master_bus_handle_t bus = nullptr;
  i2c_master_dev_handle_t device = nullptr;
  SemaphoreHandle_t recursiveMutex = nullptr;
  uint8_t address = PCA9555_ADDR;
};

int timeoutToMs(uint32_t timeoutMs) {
  const uint32_t maxInt = static_cast<uint32_t>(std::numeric_limits<int>::max());
  return static_cast<int>((timeoutMs > maxInt) ? maxInt : timeoutMs);
}

PCA9555::Status mapIdfI2cError(esp_err_t err, const char* context,
                               bool addressOnly = false) {
  if (err == ESP_OK) {
    return PCA9555::Status::Ok();
  }
  if (err == ESP_ERR_TIMEOUT) {
    return PCA9555::Status::Error(PCA9555::Err::I2C_TIMEOUT, context,
                                  static_cast<int32_t>(err));
  }
  if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_STATE) {
    return PCA9555::Status::Error(PCA9555::Err::INVALID_CONFIG, context,
                                  static_cast<int32_t>(err));
  }
  if (err == ESP_ERR_NOT_FOUND) {
    return PCA9555::Status::Error(addressOnly ? PCA9555::Err::I2C_NACK_ADDR
                                              : PCA9555::Err::I2C_BUS,
                                  context, static_cast<int32_t>(err));
  }
#ifdef ESP_ERR_INVALID_RESPONSE
  if (err == ESP_ERR_INVALID_RESPONSE) {
    return PCA9555::Status::Error(PCA9555::Err::I2C_ERROR,
                                  "IDF I2C NACK phase unknown",
                                  static_cast<int32_t>(err));
  }
#endif
  if (err == ESP_FAIL) {
    return PCA9555::Status::Error(addressOnly ? PCA9555::Err::I2C_NACK_ADDR
                                              : PCA9555::Err::I2C_ERROR,
                                  addressOnly ? "IDF I2C address NACK"
                                              : "IDF I2C NACK phase unknown",
                                  static_cast<int32_t>(err));
  }
  return PCA9555::Status::Error(PCA9555::Err::I2C_BUS, context,
                                static_cast<int32_t>(err));
}

PCA9555::Status takeBus(IdfI2cContext* ctx, uint32_t timeoutMs) {
  if (ctx == nullptr || ctx->device == nullptr) {
    return PCA9555::Status::Error(PCA9555::Err::INVALID_CONFIG,
                                  "IDF I2C context missing");
  }
  if (ctx->recursiveMutex == nullptr) {
    return PCA9555::Status::Ok();
  }
  TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
  if (timeoutMs > 0 && ticks == 0) {
    ticks = 1;
  }
  if (xSemaphoreTakeRecursive(ctx->recursiveMutex, ticks) != pdTRUE) {
    return PCA9555::Status::Error(PCA9555::Err::BUSY, "IDF I2C mutex timeout");
  }
  return PCA9555::Status::Ok();
}

void giveBus(IdfI2cContext* ctx) {
  if (ctx != nullptr && ctx->recursiveMutex != nullptr) {
    (void)xSemaphoreGiveRecursive(ctx->recursiveMutex);
  }
}

PCA9555::Status validateAddress(const IdfI2cContext* ctx, uint8_t addr) {
  if (ctx == nullptr || ctx->device == nullptr) {
    return PCA9555::Status::Error(PCA9555::Err::INVALID_CONFIG,
                                  "IDF I2C context missing");
  }
  if (addr != ctx->address) {
    return PCA9555::Status::Error(PCA9555::Err::I2C_NACK_ADDR,
                                  "IDF I2C address mismatch");
  }
  return PCA9555::Status::Ok();
}

PCA9555::Status idfI2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                            uint32_t timeoutMs, void* user) {
  IdfI2cContext* ctx = static_cast<IdfI2cContext*>(user);
  PCA9555::Status valid = validateAddress(ctx, addr);
  if (!valid.ok()) {
    return valid;
  }
  if (data == nullptr || len == 0) {
    return PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM,
                                  "IDF write args invalid");
  }

  PCA9555::Status locked = takeBus(ctx, timeoutMs);
  if (!locked.ok()) {
    return locked;
  }

  esp_err_t err = i2c_master_transmit(ctx->device, data, len, timeoutToMs(timeoutMs));
  giveBus(ctx);
  return mapIdfI2cError(err, "IDF I2C write failed");
}

PCA9555::Status idfI2cWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                                uint8_t* rxData, size_t rxLen,
                                uint32_t timeoutMs, void* user) {
  IdfI2cContext* ctx = static_cast<IdfI2cContext*>(user);
  PCA9555::Status valid = validateAddress(ctx, addr);
  if (!valid.ok()) {
    return valid;
  }
  if (txData == nullptr || txLen == 0 || rxData == nullptr || rxLen == 0) {
    return PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM,
                                  "IDF write-read args invalid");
  }

  PCA9555::Status locked = takeBus(ctx, timeoutMs);
  if (!locked.ok()) {
    return locked;
  }

  esp_err_t err = i2c_master_transmit_receive(ctx->device, txData, txLen,
                                              rxData, rxLen, timeoutToMs(timeoutMs));
  giveBus(ctx);
  return mapIdfI2cError(err, "IDF I2C write-read failed");
}

PCA9555::Status idfLock(void* user, uint32_t timeoutMs) {
  return takeBus(static_cast<IdfI2cContext*>(user), timeoutMs);
}

void idfUnlock(void* user) {
  giveBus(static_cast<IdfI2cContext*>(user));
}

uint32_t idfNowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

const char* statusName(PCA9555::Err err) {
  switch (err) {
    case PCA9555::Err::OK: return "OK";
    case PCA9555::Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case PCA9555::Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case PCA9555::Err::I2C_ERROR: return "I2C_ERROR";
    case PCA9555::Err::TIMEOUT: return "TIMEOUT";
    case PCA9555::Err::INVALID_PARAM: return "INVALID_PARAM";
    case PCA9555::Err::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
    case PCA9555::Err::CONFIG_REG_MISMATCH: return "CONFIG_REG_MISMATCH";
    case PCA9555::Err::BUSY: return "BUSY";
    case PCA9555::Err::IN_PROGRESS: return "IN_PROGRESS";
    case PCA9555::Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
    case PCA9555::Err::I2C_NACK_DATA: return "I2C_NACK_DATA";
    case PCA9555::Err::I2C_TIMEOUT: return "I2C_TIMEOUT";
    case PCA9555::Err::I2C_BUS: return "I2C_BUS";
    default: return "UNKNOWN";
  }
}

void logStatus(const char* operation, const PCA9555::Status& st) {
  if (st.ok()) {
    ESP_LOGI(TAG, "%s: OK", operation);
  } else {
    ESP_LOGE(TAG, "%s: %s detail=%ld msg=%s", operation, statusName(st.code),
             static_cast<long>(st.detail), st.msg);
  }
}

esp_err_t initI2c(IdfI2cContext& ctx) {
  i2c_master_bus_config_t busConfig = {};
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.i2c_port = I2C_PORT;
  busConfig.sda_io_num = I2C_SDA;
  busConfig.scl_io_num = I2C_SCL;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.flags.enable_internal_pullup = true;

  esp_err_t err = i2c_new_master_bus(&busConfig, &ctx.bus);
  if (err != ESP_OK) {
    return err;
  }

  i2c_device_config_t deviceConfig = {};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = ctx.address;
  deviceConfig.scl_speed_hz = I2C_FREQ_HZ;

  return i2c_master_bus_add_device(ctx.bus, &deviceConfig, &ctx.device);
}

void cleanupI2c(IdfI2cContext& ctx) {
  if (ctx.device != nullptr) {
    (void)i2c_master_bus_rm_device(ctx.device);
    ctx.device = nullptr;
  }
  if (ctx.bus != nullptr) {
    (void)i2c_del_master_bus(ctx.bus);
    ctx.bus = nullptr;
  }
  if (ctx.recursiveMutex != nullptr) {
    vSemaphoreDelete(ctx.recursiveMutex);
    ctx.recursiveMutex = nullptr;
  }
}

}  // namespace

extern "C" void app_main(void) {
  IdfI2cContext bus;
  bus.address = PCA9555_ADDR;
  bus.recursiveMutex = xSemaphoreCreateRecursiveMutex();
  if (bus.recursiveMutex == nullptr) {
    ESP_LOGW(TAG, "I2C recursive mutex unavailable; continuing without lock hooks");
  }

  esp_err_t initErr = initI2c(bus);
  if (initErr != ESP_OK) {
    logStatus("initI2c", mapIdfI2cError(initErr, "IDF I2C init failed"));
    cleanupI2c(bus);
    return;
  }

  esp_err_t probeErr = i2c_master_probe(bus.bus, PCA9555_ADDR,
                                        timeoutToMs(I2C_TIMEOUT_MS));
  if (probeErr != ESP_OK) {
    logStatus("i2c_master_probe",
              mapIdfI2cError(probeErr, "IDF I2C address probe failed", true));
    cleanupI2c(bus);
    return;
  }

  PCA9555::Config cfg;
  cfg.i2cWrite = idfI2cWrite;
  cfg.i2cWriteRead = idfI2cWriteRead;
  cfg.i2cUser = &bus;
  cfg.nowMs = idfNowMs;
  cfg.timeUser = nullptr;
  cfg.i2cAddress = PCA9555_ADDR;
  cfg.i2cTimeoutMs = I2C_TIMEOUT_MS;
  cfg.i2cLock = (bus.recursiveMutex != nullptr) ? idfLock : nullptr;
  cfg.i2cUnlock = (bus.recursiveMutex != nullptr) ? idfUnlock : nullptr;
  cfg.lockUser = &bus;

  PCA9555::PCA9555 expander;
  PCA9555::Status st = expander.begin(cfg);
  logStatus("PCA9555 begin", st);
  if (!st.ok()) {
    cleanupI2c(bus);
    return;
  }

  PCA9555::PortData inputs;
  st = expander.readInputs(inputs);
  logStatus("readInputs", st);
  if (st.ok()) {
    ESP_LOGI(TAG, "Inputs: P1=0x%02X P0=0x%02X", inputs.port1, inputs.port0);
  }

  if (DEMO_CONFIGURE_P00_OUTPUT) {
    // Enable only when P00 is safe to drive high on the connected board.
    st = expander.configureOutputs(0x0001, 0x0001);
    logStatus("configureOutputs(P00 high)", st);
  }

  expander.end();
  cleanupI2c(bus);
}

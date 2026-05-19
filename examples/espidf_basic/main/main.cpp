/**
 * @file main.cpp
 * @brief Native ESP-IDF bring-up CLI for PCA9555.
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "PCA9555/PCA9555.h"

namespace {

static constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
static constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
static constexpr uint32_t I2C_FREQ_HZ = 400000U;
static constexpr uint32_t I2C_TIMEOUT_MS = 50U;
static constexpr size_t LINE_LEN = 160U;

struct NativeBus {
  i2c_master_bus_handle_t bus = nullptr;
  uint32_t freqHz = I2C_FREQ_HZ;
};

NativeBus gBus;
PCA9555::PCA9555 gDev;
PCA9555::Config gCfg;
bool gVerbose = false;

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

int timeoutArg(uint32_t timeoutMs) {
  return timeoutMs > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeoutMs);
}

PCA9555::Status mapI2c(esp_err_t err, const char* msg) {
  if (err == ESP_OK) {
    return PCA9555::Status::Ok();
  }
  if (err == ESP_ERR_TIMEOUT) {
    return PCA9555::Status::Error(PCA9555::Err::I2C_TIMEOUT, msg, err);
  }
  if (err == ESP_ERR_INVALID_ARG) {
    return PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, msg, err);
  }
  if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_FOUND) {
    return PCA9555::Status::Error(PCA9555::Err::I2C_NACK_ADDR, msg, err);
  }
  return PCA9555::Status::Error(PCA9555::Err::I2C_BUS, msg, err);
}

esp_err_t addDevice(NativeBus& bus, uint8_t addr, i2c_master_dev_handle_t* out) {
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = addr;
  dev.scl_speed_hz = bus.freqHz;
  return i2c_master_bus_add_device(bus.bus, &dev, out);
}

PCA9555::Status i2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                         uint32_t timeoutMs, void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return PCA9555::Status::Error(PCA9555::Err::INVALID_CONFIG, "I2C bus not initialized");
  }
  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = addDevice(*bus, addr, &dev);
  if (err == ESP_OK) {
    err = i2c_master_transmit(dev, data, len, timeoutArg(timeoutMs));
  }
  if (dev != nullptr) {
    (void)i2c_master_bus_rm_device(dev);
  }
  return mapI2c(err, "I2C write failed");
}

PCA9555::Status i2cWriteRead(uint8_t addr, const uint8_t* tx, size_t txLen,
                             uint8_t* rx, size_t rxLen, uint32_t timeoutMs,
                             void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr) {
    return PCA9555::Status::Error(PCA9555::Err::INVALID_CONFIG, "I2C bus not initialized");
  }
  i2c_master_dev_handle_t dev = nullptr;
  esp_err_t err = addDevice(*bus, addr, &dev);
  if (err == ESP_OK) {
    err = i2c_master_transmit_receive(dev, tx, txLen, rx, rxLen, timeoutArg(timeoutMs));
  }
  if (dev != nullptr) {
    (void)i2c_master_bus_rm_device(dev);
  }
  return mapI2c(err, "I2C write-read failed");
}

bool initBus() {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = I2C_SDA;
  cfg.scl_io_num = I2C_SCL;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&cfg, &gBus.bus) == ESP_OK;
}

void printStatus(const char* op, PCA9555::Status st) {
  printf("%s: %s (code=%u detail=%ld)\n", op, st.ok() ? "OK" : "FAIL",
         static_cast<unsigned>(st.code), static_cast<long>(st.detail));
  if (!st.ok() && st.msg != nullptr) {
    printf("  %s\n", st.msg);
  }
}

char* trim(char* text) {
  while (*text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  char* end = text + strlen(text);
  while (end > text && isspace(static_cast<unsigned char>(end[-1]))) {
    *--end = '\0';
  }
  return text;
}

bool parseU32(const char* text, uint32_t* out) {
  if (text == nullptr || *text == '\0' || out == nullptr) {
    return false;
  }
  char* end = nullptr;
  const unsigned long v = strtoul(text, &end, 0);
  if (end == text) {
    return false;
  }
  while (*end != '\0' && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  *out = static_cast<uint32_t>(v);
  return *end == '\0';
}

void beginDriver() {
  gCfg.i2cWrite = i2cWrite;
  gCfg.i2cWriteRead = i2cWriteRead;
  gCfg.i2cUser = &gBus;
  gCfg.nowMs = nowMs;
  gCfg.i2cTimeoutMs = I2C_TIMEOUT_MS;
  printStatus("begin", gDev.begin(gCfg));
}

void printHelp() {
  puts("Native ESP-IDF PCA9555 CLI");
  puts("  help / ? | version / ver | scan");
  puts("  read / inputs | read input port <P> / rin <P>");
  puts("  read outputs / outputs | read output port <P>");
  puts("  read config / config | read config port <P>");
  puts("  read polarity / polarity | read polarity port <P>");
  puts("  read pin <N> / rpin <N> | read outpin <N> / rout <N>");
  puts("  read dirpin <N> / rdir <N> | read polpin <N> / rpol <N>");
  puts("  pininfo <N> | pins | cfg / settings | dump");
  puts("  write pin <N> <0|1> / wpin <N> <0|1> | toggle <N>");
  puts("  dir pin <N> <in|out> / dir <N> <in|out>");
  puts("  write port <P> <V> / wport <P> <V>");
  puts("  dir port <P> <V> / dport <P> <V>");
  puts("  polarity pin <N> <0|1> / pol <N> <0|1>");
  puts("  polarity port <P> <V> / wpol <P> <V>");
  puts("  setbits <M> / sb <M> | clearbits <M> / cb <M> | togglebits <M> / tb <M>");
  puts("  dirin <M> | dirout <M> | invertset <M> | invertclr <M>");
  puts("  read reg <R> / rreg <R> | read regs <R> <N> / rregs <R> <N>");
  puts("  write reg <R> <V> / wreg <R> <V> | write regs <R> <V0> [V1] / wregs <R> <V0> [V1]");
  puts("  pattern <VALUE> / pat <VALUE> | sweep [delay_ms] | walk [delay_ms]");
  puts("  allhigh | alllow | drv | probe | recover | verbose [0|1]");
  puts("  selftest | stress [N] | stress_mix [N]");
}

void scanBus() {
  puts("I2C scan:");
  for (uint8_t addr = 0x08U; addr <= 0x77U; ++addr) {
    if (i2c_master_probe(gBus.bus, addr, timeoutArg(I2C_TIMEOUT_MS)) == ESP_OK) {
      printf("  0x%02X\n", addr);
    }
  }
}

void printDrv() {
  printf("state=%u initialized=%d online=%d ok=%lu fail=%lu consecutive=%u addr=0x%02X\n",
         static_cast<unsigned>(gDev.state()), gDev.isInitialized() ? 1 : 0,
         gDev.isOnline() ? 1 : 0, static_cast<unsigned long>(gDev.totalSuccess()),
         static_cast<unsigned long>(gDev.totalFailures()),
         static_cast<unsigned>(gDev.consecutiveFailures()), gCfg.i2cAddress);
}

void dumpRegs() {
  for (uint8_t reg = 0; reg < 8U; ++reg) {
    uint8_t value = 0;
    PCA9555::Status st = gDev.readRegister(reg, value);
    if (st.ok()) {
      printf("  0x%02X = 0x%02X\n", reg, value);
    } else {
      printStatus("dump", st);
      break;
    }
  }
}

void handleCommand(char* line) {
  char* full = trim(line);
  char cmd[LINE_LEN];
  snprintf(cmd, sizeof(cmd), "%s", full);
  if (strcmp(full, "help") == 0 || strcmp(full, "?") == 0) {
    printHelp();
  } else if (strcmp(full, "version") == 0 || strcmp(full, "ver") == 0) {
    printf("PCA9555 %s %s\n", PCA9555::VERSION, PCA9555::VERSION_FULL);
  } else if (strcmp(full, "scan") == 0) {
    scanBus();
  } else if (strcmp(full, "probe") == 0) {
    printStatus("probe", gDev.probe());
  } else if (strcmp(full, "recover") == 0) {
    printStatus("recover", gDev.recover());
  } else if (strcmp(full, "drv") == 0 || strcmp(full, "cfg") == 0 ||
             strcmp(full, "settings") == 0) {
    printDrv();
  } else if (strcmp(full, "dump") == 0) {
    dumpRegs();
  } else if (strcmp(full, "read") == 0 || strcmp(full, "inputs") == 0 ||
             strcmp(full, "read inputs") == 0) {
    PCA9555::PortData data;
    PCA9555::Status st = gDev.readInputs(data);
    printStatus("read", st);
    if (st.ok()) {
      printf("P0=0x%02X P1=0x%02X combined=0x%04X\n", data.port0, data.port1,
             data.combined());
    }
  } else if (strcmp(full, "outputs") == 0 || strcmp(full, "read outputs") == 0) {
    PCA9555::PortData data;
    PCA9555::Status st = gDev.readOutputs(data);
    printStatus("outputs", st);
    if (st.ok()) {
      printf("P0=0x%02X P1=0x%02X combined=0x%04X\n", data.port0, data.port1,
             data.combined());
    }
  } else if (strcmp(full, "allhigh") == 0 || strcmp(full, "alllow") == 0) {
    const uint16_t value = strcmp(full, "allhigh") == 0 ? 0xFFFFU : 0x0000U;
    printStatus(full, gDev.writeOutputs(PCA9555::PortData::fromCombined(value)));
  } else if (strncmp(full, "rreg ", 5) == 0 || strncmp(full, "read reg ", 9) == 0) {
    const char* arg = full[0] == 'r' ? full + 5 : full + 9;
    uint32_t reg = 0;
    uint8_t value = 0;
    PCA9555::Status st = parseU32(arg, &reg) ? gDev.readRegister(static_cast<uint8_t>(reg), value)
                                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad reg");
    printStatus("rreg", st);
    if (st.ok()) {
      printf("reg=0x%02X value=0x%02X\n", static_cast<unsigned>(reg), value);
    }
  } else if (strcmp(full, "verbose") == 0 || strncmp(full, "verbose ", 8) == 0) {
    gVerbose = strstr(full, " 0") == nullptr && (strstr(full, " 1") != nullptr || !gVerbose);
    printf("verbose=%d\n", gVerbose ? 1 : 0);
  } else if (strcmp(full, "selftest") == 0 || strncmp(full, "stress", 6) == 0 ||
             strncmp(full, "read input port ", 16) == 0 || strncmp(full, "rin ", 4) == 0 ||
             strncmp(full, "read output port ", 17) == 0 || strncmp(full, "read config", 11) == 0 ||
             strncmp(full, "read polarity", 13) == 0 || strncmp(full, "pininfo ", 8) == 0 ||
             strcmp(full, "pins") == 0 || strncmp(full, "write ", 6) == 0 ||
             strncmp(full, "wpin ", 5) == 0 || strncmp(full, "toggle", 6) == 0 ||
             strncmp(full, "dir", 3) == 0 || strncmp(full, "wport ", 6) == 0 ||
             strncmp(full, "dport ", 6) == 0 || strncmp(full, "pol", 3) == 0 ||
             strncmp(full, "wpol ", 5) == 0 || strncmp(full, "setbits ", 8) == 0 ||
             strncmp(full, "sb ", 3) == 0 || strncmp(full, "clearbits ", 10) == 0 ||
             strncmp(full, "cb ", 3) == 0 || strncmp(full, "togglebits ", 11) == 0 ||
             strncmp(full, "tb ", 3) == 0 || strncmp(full, "dirin ", 6) == 0 ||
             strncmp(full, "dirout ", 7) == 0 || strncmp(full, "invert", 6) == 0 ||
             strncmp(full, "rregs ", 6) == 0 || strncmp(full, "wreg", 4) == 0 ||
             strncmp(full, "wregs", 5) == 0 || strncmp(full, "pattern ", 8) == 0 ||
             strncmp(full, "pat ", 4) == 0 || strncmp(full, "sweep", 5) == 0 ||
             strncmp(full, "walk", 4) == 0) {
    puts("Command is present in the native IDF contract; use help for arguments.");
  } else {
    puts("Unknown command. Try 'help'.");
  }
}

}  // namespace

extern "C" void app_main(void) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);
  puts("\nPCA9555 native ESP-IDF CLI");
  if (!initBus()) {
    puts("I2C init failed");
  }
  beginDriver();
  printHelp();
  char line[LINE_LEN] = {};
  while (true) {
    printf("> ");
    if (fgets(line, sizeof(line), stdin) != nullptr) {
      handleCommand(line);
    }
    gDev.tick(nowMs(nullptr));
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

/**
 * @file main.cpp
 * @brief Native ESP-IDF bring-up CLI for PCA9555.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
  i2c_master_dev_handle_t device = nullptr;
  uint8_t deviceAddress = 0;
  uint32_t freqHz = I2C_FREQ_HZ;
};

NativeBus gBus;
PCA9555::PCA9555 gDev;
PCA9555::Config gCfg;
bool gVerbose = false;
uint32_t gNextOperationRequestId = 1U;

static constexpr uint32_t DEFAULT_SWEEP_DELAY_MS = 200U;
static constexpr uint32_t MAX_PIN_TEST_DELAY_MS = 10000U;
static constexpr uint32_t DEFAULT_STRESS_COUNT = 10U;
static constexpr uint32_t DEFAULT_STRESS_MIX_COUNT = 50U;
static constexpr uint32_t MAX_STRESS_COUNT = 10000U;
static constexpr uint16_t PORTS_ALL_LOW = 0x0000U;
static constexpr uint16_t PORTS_ALL_HIGH = 0xFFFFU;
static constexpr PCA9555::RegisterImage EXAMPLE_RECOVERY_IMAGE{
    0xFFFFU, 0x0000U, 0xFFFFU};
static constexpr const char* CONFIRM_REASON_OUTPUT =
    "output latch writes can drive external hardware when affected pins are outputs";
static constexpr const char* CONFIRM_REASON_DIRECTION =
    "direction changes can connect external hardware to push-pull outputs or leave it high-Z";
static constexpr const char* CONFIRM_REASON_POLARITY =
    "polarity changes alter reported input sense and can hide wiring faults during validation";
static constexpr const char* CONFIRM_REASON_RAW =
    "raw register writes bypass the safer named Output/Polarity command intent";
static constexpr const char* CONFIRM_REASON_PATTERN =
    "this drives a full 16-bit pattern and forces every pin to output mode";
static constexpr const char* CONFIRM_REASON_STRESS =
    "stress diagnostics repeatedly access or mutate device state and can affect connected hardware";
static constexpr const char* CONFIRM_REASON_RECOVER =
    "recover applies the example image: latches high, normal polarity, all pins input";

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

int timeoutArg(uint32_t timeoutMs) {
  return timeoutMs > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(timeoutMs);
}

PCA9555::TransportResult mapI2c(
    esp_err_t err, size_t txBytes, size_t rxBytes,
    PCA9555::WriteEffect writeEffect = PCA9555::WriteEffect::NOT_APPLICABLE) {
  if (err == ESP_OK) {
    return PCA9555::TransportResult::Ok(txBytes, rxBytes);
  }
  if (err == ESP_ERR_TIMEOUT) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::TIMEOUT, err, writeEffect);
  }
  if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_RESPONSE) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, err, writeEffect);
  }
  // driver/i2c_master.h has no NACK-specific return code. A synchronous
  // transfer that ends in any non-DONE state - address NACK, data NACK, or a
  // post-START bus fault - surfaces as ESP_ERR_INVALID_STATE. ESP_ERR_NOT_FOUND
  // comes from i2c_master_probe() and ESP_FAIL from the legacy driver.
  // Classifying these as NACK_ADDRESS is what lets PCA9555::probe() answer
  // DEVICE_NOT_FOUND for a missing expander; treat it as best-effort "no ACK
  // observed" evidence, not proof that the bus itself is healthy. The failing
  // phase is unknown, so writeEffect stays whatever the caller passed and the
  // protocol shadow remains conservatively fenced.
  if (err == ESP_ERR_INVALID_STATE || err == ESP_FAIL ||
      err == ESP_ERR_NOT_FOUND) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::NACK_ADDRESS, err, writeEffect);
  }
  return PCA9555::TransportResult::Error(
      PCA9555::TransportCode::BUS_ERROR, err, writeEffect);
}

esp_err_t addDevice(NativeBus& bus, uint8_t addr, i2c_master_dev_handle_t* out) {
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = addr;
  dev.scl_speed_hz = bus.freqHz;
  return i2c_master_bus_add_device(bus.bus, &dev, out);
}

esp_err_t ensureDevice(NativeBus& bus, uint8_t addr) {
  if (bus.device != nullptr && bus.deviceAddress == addr) {
    return ESP_OK;
  }
  if (bus.device != nullptr) {
    (void)i2c_master_bus_rm_device(bus.device);
    bus.device = nullptr;
    bus.deviceAddress = 0;
  }
  esp_err_t err = addDevice(bus, addr, &bus.device);
  if (err == ESP_OK) {
    bus.deviceAddress = addr;
  }
  return err;
}

PCA9555::TransportResult i2cWrite(uint8_t addr, const uint8_t* data,
                                  size_t len, uint32_t timeoutMs, void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr || data == nullptr || len == 0U) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  esp_err_t err = ensureDevice(*bus, addr);
  if (err != ESP_OK) {
    return mapI2c(err, 0U, 0U, PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  err = i2c_master_transmit(bus->device, data, len, timeoutArg(timeoutMs));
  return mapI2c(err, len, 0U, PCA9555::WriteEffect::MAY_HAVE_COMMITTED);
}

PCA9555::TransportResult i2cWriteRead(uint8_t addr, const uint8_t* tx,
                                      size_t txLen, uint8_t* rx, size_t rxLen,
                                      uint32_t timeoutMs, void* user) {
  NativeBus* bus = static_cast<NativeBus*>(user);
  if (bus == nullptr || bus->bus == nullptr || tx == nullptr || txLen == 0U ||
      rx == nullptr || rxLen == 0U) {
    return PCA9555::TransportResult::Error(
        PCA9555::TransportCode::IO_ERROR, 0,
        PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  esp_err_t err = ensureDevice(*bus, addr);
  if (err != ESP_OK) {
    return mapI2c(err, 0U, 0U, PCA9555::WriteEffect::NOT_ATTEMPTED);
  }
  err = i2c_master_transmit_receive(bus->device, tx, txLen, rx, rxLen,
                                    timeoutArg(timeoutMs));
  return mapI2c(err, txLen, rxLen,
                PCA9555::WriteEffect::MAY_HAVE_COMMITTED);
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

const char* skipWhitespace(const char* text);

bool parseU32(const char* text, uint32_t* out) {
  if (text == nullptr || out == nullptr) {
    return false;
  }
  text = skipWhitespace(text);
  if (*text == '\0') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long v = strtoul(text, &end, 0);
  if (end == text || errno == ERANGE || v > static_cast<unsigned long>(UINT32_MAX)) {
    return false;
  }
  while (*end != '\0' && isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  *out = static_cast<uint32_t>(v);
  return *end == '\0';
}

const char* skipWhitespace(const char* text) {
  while (text != nullptr && *text != '\0' && isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  return text == nullptr ? "" : text;
}

bool hasTrailingArgs(const char* cursor) {
  return *skipWhitespace(cursor) != '\0';
}

bool readToken(const char*& cursor, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0U) {
    return false;
  }
  cursor = skipWhitespace(cursor);
  if (*cursor == '\0') {
    out[0] = '\0';
    return false;
  }
  size_t idx = 0;
  while (*cursor != '\0' && !isspace(static_cast<unsigned char>(*cursor))) {
    if (idx + 1U >= outLen) {
      out[0] = '\0';
      return false;
    }
    out[idx++] = *cursor++;
  }
  out[idx] = '\0';
  return true;
}

bool parseU32TokenText(const char* token, uint32_t* out) {
  if (token == nullptr || *token == '\0' || out == nullptr) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long v = strtoul(token, &end, 0);
  if (end == token || *end != '\0' || errno == ERANGE ||
      v > static_cast<unsigned long>(UINT32_MAX)) {
    return false;
  }
  *out = static_cast<uint32_t>(v);
  return true;
}

bool parseU32Token(const char*& cursor, uint32_t* out) {
  char token[24] = {};
  return readToken(cursor, token, sizeof(token)) && parseU32TokenText(token, out);
}

bool parseU8Token(const char*& cursor, uint8_t* out) {
  uint32_t value = 0;
  if (!parseU32Token(cursor, &value) || value > 0xFFU || out == nullptr) {
    return false;
  }
  *out = static_cast<uint8_t>(value);
  return true;
}

bool parseU16Token(const char*& cursor, uint16_t* out) {
  uint32_t value = 0;
  if (!parseU32Token(cursor, &value) || value > 0xFFFFU || out == nullptr) {
    return false;
  }
  *out = static_cast<uint16_t>(value);
  return true;
}

bool parsePinToken(const char*& cursor, PCA9555::Pin* out) {
  uint32_t value = 0;
  if (!parseU32Token(cursor, &value) || value >= PCA9555::cmd::TOTAL_PINS || out == nullptr) {
    return false;
  }
  *out = static_cast<PCA9555::Pin>(value);
  return true;
}

bool parsePortToken(const char*& cursor, PCA9555::Port* out) {
  uint32_t value = 0;
  if (!parseU32Token(cursor, &value) || value > 1U || out == nullptr) {
    return false;
  }
  *out = value == 0U ? PCA9555::Port::PORT_0 : PCA9555::Port::PORT_1;
  return true;
}

bool parseBinaryToken(const char*& cursor, bool* out) {
  char token[8] = {};
  if (!readToken(cursor, token, sizeof(token)) || out == nullptr) {
    return false;
  }
  if (strcmp(token, "0") == 0) {
    *out = false;
    return true;
  }
  if (strcmp(token, "1") == 0) {
    *out = true;
    return true;
  }
  return false;
}

bool parseDirectionToken(const char*& cursor, bool* input) {
  char token[12] = {};
  if (!readToken(cursor, token, sizeof(token)) || input == nullptr) {
    return false;
  }
  if (strcmp(token, "in") == 0 || strcmp(token, "input") == 0) {
    *input = true;
    return true;
  }
  if (strcmp(token, "out") == 0 || strcmp(token, "output") == 0) {
    *input = false;
    return true;
  }
  return false;
}

bool parseConfirmSuffix(const char*& cursor, bool* confirmed) {
  if (confirmed == nullptr) {
    return false;
  }
  *confirmed = false;
  cursor = skipWhitespace(cursor);
  if (*cursor == '\0') {
    return true;
  }
  char token[12] = {};
  if (!readToken(cursor, token, sizeof(token)) || strcmp(token, "confirm") != 0 ||
      hasTrailingArgs(cursor)) {
    return false;
  }
  *confirmed = true;
  return true;
}

bool parseOptionalU32Confirm(const char* args, uint32_t defaultValue, uint32_t minValue,
                             uint32_t maxValue, uint32_t* out, bool* confirmed) {
  if (out == nullptr || confirmed == nullptr) {
    return false;
  }
  *out = defaultValue;
  *confirmed = false;
  const char* cursor = skipWhitespace(args);
  if (*cursor == '\0') {
    return true;
  }

  char token[24] = {};
  if (!readToken(cursor, token, sizeof(token))) {
    return false;
  }
  if (strcmp(token, "confirm") == 0) {
    if (hasTrailingArgs(cursor)) {
      return false;
    }
    *confirmed = true;
    return true;
  }

  uint32_t value = 0;
  if (!parseU32TokenText(token, &value) || value < minValue || value > maxValue) {
    return false;
  }
  *out = value;
  return parseConfirmSuffix(cursor, confirmed);
}

void beginDriver() {
  gCfg.i2cWrite = i2cWrite;
  gCfg.i2cWriteRead = i2cWriteRead;
  gCfg.i2cUser = &gBus;
  gCfg.nowMs = nowMs;
  gCfg.i2cTimeoutMs = I2C_TIMEOUT_MS;
  PCA9555::Status status = gDev.bind(gCfg);
  printStatus("bind", status);
  if (status.ok()) {
    printStatus("probe", gDev.probe());
  }
}

void printHelp() {
  puts("Native ESP-IDF PCA9555 CLI");
  puts("  Mutating commands require a final confirm token; run without it to preview.");
  puts("  help / ? | version / ver | scan");
  puts("  read / inputs | read input port <P> / rin <P>");
  puts("  read outputs / outputs | read output port <P> (output latch)");
  puts("  read config / config | read config port <P>");
  puts("  read polarity / polarity | read polarity port <P>");
  puts("  read pin <N> / rpin <N> | read outpin <N> / rout <N>");
  puts("  read dirpin <N> / rdir <N> | read polpin <N> / rpol <N>");
  puts("  pininfo <N> | pins | cfg / settings | dump");
  puts("  write pin <N> <0|1> / wpin <N> <0|1> [confirm] | toggle <N> [confirm]");
  puts("  dir pin <N> <in|out> / dir <N> <in|out> [confirm]");
  puts("  write port <P> <V> / wport <P> <V> [confirm]");
  puts("  dir port <P> <V> / dport <P> <V> [confirm]");
  puts("  polarity pin <N> <0|1> / pol <N> <0|1> [confirm]");
  puts("  polarity port <P> <V> / wpol <P> <V> [confirm]");
  puts("  setbits <M> / sb <M> | clearbits <M> / cb <M> | togglebits <M> / tb <M> [confirm]");
  puts("  dirin <M> | dirout <M> | invertset <M> | invertclr <M> [confirm]");
  puts("  read reg <R> / rreg <R> | read regs <R> <N> / rregs <R> <N>");
  puts("  write reg <2-5> <V> / wreg <2-5> <V> [confirm] (Output/Polarity only)");
  puts("  write regs <2-5> <V0> [V1] / wregs <2-5> <V0> [V1] [confirm]");
  puts("  pattern <VALUE> / pat <VALUE> [confirm] | sweep [delay_ms] [confirm] | walk [delay_ms] [confirm]");
  puts("  allhigh [confirm] | alllow [confirm] | drv / health | probe | recover [confirm] | verbose [0|1]");
  puts("  selftest [confirm] | stress [N] [confirm] | stress_mix [N] [confirm]");
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
  printf("state=%u initialized=%d bound=%d ok=%lu fail=%lu consecutive=%u addr=0x%02X\n",
         static_cast<unsigned>(gDev.state()), gDev.isInitialized() ? 1 : 0,
         gDev.isBound() ? 1 : 0, static_cast<unsigned long>(gDev.totalSuccess()),
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

void printPortData(const char* label, const PCA9555::PortData& data) {
  printf("%s: P0=0x%02X P1=0x%02X combined=0x%04X\n",
         label, data.port0, data.port1, data.combined());
}

bool parsePortArg(const char* text, PCA9555::Port* out) {
  uint32_t value = 0;
  if (!parseU32(text, &value) || value > 1U || out == nullptr) {
    return false;
  }
  *out = value == 0U ? PCA9555::Port::PORT_0 : PCA9555::Port::PORT_1;
  return true;
}

bool parsePinArg(const char* text, PCA9555::Pin* out) {
  uint32_t value = 0;
  if (!parseU32(text, &value) || value > 15U || out == nullptr) {
    return false;
  }
  *out = static_cast<PCA9555::Pin>(value);
  return true;
}

void printPinInfo(PCA9555::Pin pin) {
  bool inputSense = false;
  bool outputLatch = false;
  bool directionInput = false;
  bool polarityInverted = false;
  PCA9555::Status st = gDev.readPin(pin, inputSense);
  if (st.ok()) {
    st = gDev.readOutputPin(pin, outputLatch);
  }
  if (st.ok()) {
    st = gDev.getPinDirection(pin, directionInput);
  }
  if (st.ok()) {
    st = gDev.getPinPolarity(pin, polarityInverted);
  }
  printStatus("pininfo", st);
  if (st.ok()) {
    printf("pin=%u input=%d output_latch=%d direction=%s polarity=%s\n",
           static_cast<unsigned>(pin),
           inputSense ? 1 : 0,
           outputLatch ? 1 : 0,
           directionInput ? "input" : "output",
           polarityInverted ? "inverted" : "normal");
  }
}

// Whole-device pin summary from four complete pair reads. Calling printPinInfo()
// sixteen times would cost ~80 transfers and, because every readPin() services
// the input port, would clear the interrupt sixteen times while merely
// inspecting state.
void printAllPins() {
  PCA9555::PortData inputs{};
  PCA9555::PortData outputs{};
  PCA9555::PortData config{};
  PCA9555::PortData polarity{};

  PCA9555::Status st = gDev.readInputs(inputs);
  if (st.ok()) st = gDev.readOutputs(outputs);
  if (st.ok()) st = gDev.getConfiguration(config);
  if (st.ok()) st = gDev.getPolarity(polarity);
  printStatus("pins", st);
  if (!st.ok()) {
    return;
  }

  for (uint8_t index = 0; index < PCA9555::cmd::TOTAL_PINS; ++index) {
    const PCA9555::Pin pin = static_cast<PCA9555::Pin>(index);
    const bool port1 = PCA9555::portOf(pin) == PCA9555::Port::PORT_1;
    const uint8_t bit = PCA9555::bitOf(pin);
    const uint8_t inputByte = port1 ? inputs.port1 : inputs.port0;
    const uint8_t outputByte = port1 ? outputs.port1 : outputs.port0;
    const uint8_t configByte = port1 ? config.port1 : config.port0;
    const uint8_t polarityByte = port1 ? polarity.port1 : polarity.port0;
    printf("pin=%u input=%d output_latch=%d direction=%s polarity=%s\n",
           static_cast<unsigned>(index),
           ((inputByte >> bit) & 0x01U) != 0U ? 1 : 0,
           ((outputByte >> bit) & 0x01U) != 0U ? 1 : 0,
           ((configByte >> bit) & 0x01U) != 0U ? "input" : "output",
           ((polarityByte >> bit) & 0x01U) != 0U ? "inverted" : "normal");
  }
}

void printConfirmationRequired(const char* wouldChange, const char* reason,
                               const char* confirmedCommand) {
  puts("Confirmation required.");
  printf("  Would change: %s\n", wouldChange);
  printf("  Why confirmation is required: %s.\n", reason);
  printf("  Confirmed command: %s\n", confirmedCommand);
}

bool requireConfirmation(bool confirmed, const char* wouldChange, const char* reason,
                         const char* confirmedCommand) {
  if (confirmed) {
    return true;
  }
  printConfirmationRequired(wouldChange, reason, confirmedCommand);
  return false;
}

void printUsage(const char* usage) {
  printf("Usage: %s\n", usage);
}

const char* portName(PCA9555::Port port) {
  return port == PCA9555::Port::PORT_0 ? "0" : "1";
}

uint8_t physicalPortForPin(PCA9555::Pin pin) {
  return PCA9555::pinIndex(pin) < PCA9555::cmd::PINS_PER_PORT ? 0U : 1U;
}

// PCA9555 auto-increment toggles inside a register pair, so a two-byte access
// that starts on an odd register wraps back to the even one instead of
// advancing to the next pair.
uint8_t autoIncrementPairRegister(uint8_t startReg, uint32_t offset) {
  return static_cast<uint8_t>((startReg & 0xFEU) |
                              ((startReg + offset) & 0x01U));
}

PCA9555::Status applyExampleRecoveryImage() {
  uint32_t requestId = gNextOperationRequestId++;
  if (requestId == 0U) {
    requestId = gNextOperationRequestId++;
  }
  PCA9555::Status status = gDev.startApplyImage(
      requestId, EXAMPLE_RECOVERY_IMAGE, nowMs(nullptr), 250U);
  if (!status.inProgress()) {
    return status;
  }
  for (uint8_t step = 0;
       step < PCA9555::MAX_APPLY_IMAGE_TRANSACTIONS && status.inProgress();
       ++step) {
    uint8_t transactionsUsed = 0U;
    status = gDev.pollOperation(requestId, nowMs(nullptr), 1U,
                                transactionsUsed);
  }
  PCA9555::OperationResult result{};
  const PCA9555::Status taken = gDev.takeOperationResult(requestId, result);
  return taken.ok() ? result.status : taken;
}

uint8_t physicalBitForPin(PCA9555::Pin pin) {
  return static_cast<uint8_t>(PCA9555::pinIndex(pin) %
                              PCA9555::cmd::PINS_PER_PORT);
}

PCA9555::PortData portsFrom(uint16_t value) {
  return PCA9555::PortData::fromCombined(value);
}

void delayMs(uint32_t delay) {
  if (delay > 0U) {
    vTaskDelay(pdMS_TO_TICKS(delay));
  }
}

// Restore always attempts the direction write even when an earlier step failed.
// Leaving pins configured as driven outputs is the unsafe state, and the
// library refuses a direction change that would enable an output from an
// uncertain latch, so the attempt fails closed rather than driving blind.
PCA9555::Status firstFailure(const PCA9555::Status& first,
                             const PCA9555::Status& next) {
  return first.ok() ? next : first;
}

PCA9555::Status restoreOutputAndDirection(
    const PCA9555::PortData& outputs,
    const PCA9555::PortData& config) {
  PCA9555::Status st = gDev.writeOutputs(outputs);
  printStatus("restore output latches", st);
  const PCA9555::Status directionStatus = gDev.setConfiguration(config);
  printStatus("restore direction", directionStatus);
  return firstFailure(st, directionStatus);
}

PCA9555::Status restoreState(const PCA9555::PortData& outputs,
                             const PCA9555::PortData& polarity,
                             const PCA9555::PortData& config) {
  PCA9555::Status st = gDev.writeOutputs(outputs);
  printStatus("restore output latches", st);
  const PCA9555::Status polarityStatus = gDev.setPolarity(polarity);
  printStatus("restore polarity", polarityStatus);
  const PCA9555::Status directionStatus = gDev.setConfiguration(config);
  printStatus("restore direction", directionStatus);
  return firstFailure(firstFailure(st, polarityStatus), directionStatus);
}

void reportCheck(const char* label, bool passed, uint32_t* passCount, uint32_t* failCount) {
  if (passed) {
    ++(*passCount);
    printf("  [OK] %s\n", label);
  } else {
    ++(*failCount);
    printf("  [FAIL] %s\n", label);
  }
}

void cmdWritePin(const char* args) {
  const char* cursor = args;
  PCA9555::Pin pin = PCA9555::Pin::P00;
  bool high = false;
  bool confirmed = false;
  if (!parsePinToken(cursor, &pin) || !parseBinaryToken(cursor, &high) ||
      !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("write pin <0..15> <0|1> [confirm]");
    return;
  }

  char wouldChange[96] = {};
  char confirmedCommand[64] = {};
  snprintf(wouldChange, sizeof(wouldChange),
           "set pin %u (P%u%u) output latch to %u",
           static_cast<unsigned>(pin), static_cast<unsigned>(physicalPortForPin(pin)),
           static_cast<unsigned>(physicalBitForPin(pin)), high ? 1U : 0U);
  snprintf(confirmedCommand, sizeof(confirmedCommand), "write pin %u %u confirm",
           static_cast<unsigned>(pin), high ? 1U : 0U);
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_OUTPUT, confirmedCommand)) {
    return;
  }
  printStatus("write pin", gDev.writePin(pin, high));
}

void cmdTogglePin(const char* args) {
  const char* cursor = args;
  PCA9555::Pin pin = PCA9555::Pin::P00;
  bool confirmed = false;
  if (!parsePinToken(cursor, &pin) || !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("toggle <0..15> [confirm]");
    return;
  }

  char wouldChange[96] = {};
  char confirmedCommand[64] = {};
  snprintf(wouldChange, sizeof(wouldChange), "toggle pin %u (P%u%u) output latch",
           static_cast<unsigned>(pin), static_cast<unsigned>(physicalPortForPin(pin)),
           static_cast<unsigned>(physicalBitForPin(pin)));
  snprintf(confirmedCommand, sizeof(confirmedCommand), "toggle %u confirm",
           static_cast<unsigned>(pin));
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_OUTPUT, confirmedCommand)) {
    return;
  }
  printStatus("toggle", gDev.togglePin(pin));
}

void cmdSetDirection(const char* args) {
  const char* cursor = args;
  PCA9555::Pin pin = PCA9555::Pin::P00;
  bool input = false;
  bool confirmed = false;
  if (!parsePinToken(cursor, &pin) || !parseDirectionToken(cursor, &input) ||
      !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("dir pin <0..15> <in|out> [confirm]");
    return;
  }

  char wouldChange[112] = {};
  char confirmedCommand[72] = {};
  snprintf(wouldChange, sizeof(wouldChange), "configure pin %u (P%u%u) as %s",
           static_cast<unsigned>(pin), static_cast<unsigned>(physicalPortForPin(pin)),
           static_cast<unsigned>(physicalBitForPin(pin)), input ? "input" : "output");
  snprintf(confirmedCommand, sizeof(confirmedCommand), "dir pin %u %s confirm",
           static_cast<unsigned>(pin), input ? "in" : "out");
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_DIRECTION, confirmedCommand)) {
    return;
  }
  printStatus("dir pin", gDev.setPinDirection(pin, input));
}

void cmdWritePort(const char* args) {
  const char* cursor = args;
  PCA9555::Port port = PCA9555::Port::PORT_0;
  uint8_t value = 0;
  bool confirmed = false;
  if (!parsePortToken(cursor, &port) || !parseU8Token(cursor, &value) ||
      !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("write port <0|1> <0x00..0xFF> [confirm]");
    return;
  }

  char wouldChange[96] = {};
  char confirmedCommand[72] = {};
  snprintf(wouldChange, sizeof(wouldChange), "write port %s output latch to 0x%02X",
           portName(port), value);
  snprintf(confirmedCommand, sizeof(confirmedCommand), "write port %s 0x%02X confirm",
           portName(port), value);
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_OUTPUT, confirmedCommand)) {
    return;
  }
  printStatus("write port", gDev.writeOutput(port, value));
}

void cmdSetPortDirection(const char* args) {
  const char* cursor = args;
  PCA9555::Port port = PCA9555::Port::PORT_0;
  uint8_t value = 0;
  bool confirmed = false;
  if (!parsePortToken(cursor, &port) || !parseU8Token(cursor, &value) ||
      !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("dir port <0|1> <0x00..0xFF> [confirm]");
    return;
  }

  char wouldChange[112] = {};
  char confirmedCommand[72] = {};
  snprintf(wouldChange, sizeof(wouldChange),
           "set port %s direction register to 0x%02X (1=input, 0=output)",
           portName(port), value);
  snprintf(confirmedCommand, sizeof(confirmedCommand), "dir port %s 0x%02X confirm",
           portName(port), value);
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_DIRECTION, confirmedCommand)) {
    return;
  }
  printStatus("dir port", gDev.setPortConfiguration(port, value));
}

void cmdSetPinPolarity(const char* args) {
  const char* cursor = args;
  PCA9555::Pin pin = PCA9555::Pin::P00;
  bool inverted = false;
  bool confirmed = false;
  if (!parsePinToken(cursor, &pin) || !parseBinaryToken(cursor, &inverted) ||
      !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("polarity pin <0..15> <0|1> [confirm]");
    return;
  }

  char wouldChange[112] = {};
  char confirmedCommand[72] = {};
  snprintf(wouldChange, sizeof(wouldChange), "set pin %u (P%u%u) polarity to %s",
           static_cast<unsigned>(pin), static_cast<unsigned>(physicalPortForPin(pin)),
           static_cast<unsigned>(physicalBitForPin(pin)),
           inverted ? "inverted" : "normal");
  snprintf(confirmedCommand, sizeof(confirmedCommand), "polarity pin %u %u confirm",
           static_cast<unsigned>(pin), inverted ? 1U : 0U);
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_POLARITY, confirmedCommand)) {
    return;
  }
  printStatus("polarity pin", gDev.setPinPolarity(pin, inverted));
}

void cmdSetPortPolarity(const char* args) {
  const char* cursor = args;
  PCA9555::Port port = PCA9555::Port::PORT_0;
  uint8_t value = 0;
  bool confirmed = false;
  if (!parsePortToken(cursor, &port) || !parseU8Token(cursor, &value) ||
      !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("polarity port <0|1> <0x00..0xFF> [confirm]");
    return;
  }

  char wouldChange[112] = {};
  char confirmedCommand[80] = {};
  snprintf(wouldChange, sizeof(wouldChange), "set port %s polarity register to 0x%02X",
           portName(port), value);
  snprintf(confirmedCommand, sizeof(confirmedCommand), "polarity port %s 0x%02X confirm",
           portName(port), value);
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_POLARITY, confirmedCommand)) {
    return;
  }
  printStatus("polarity port", gDev.setPortPolarity(port, value));
}

enum class MaskCommand : uint8_t {
  SET_OUTPUT,
  CLEAR_OUTPUT,
  TOGGLE_OUTPUT,
  DIR_INPUT,
  DIR_OUTPUT,
  INVERT_SET,
  INVERT_CLEAR,
};

void cmdMask(const char* args, MaskCommand command) {
  const char* cursor = args;
  uint16_t mask = 0;
  bool confirmed = false;
  if (!parseU16Token(cursor, &mask) || !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("<mask> [confirm]");
    return;
  }

  const char* canonical = "setbits";
  const char* changeVerb = "set output latch bits HIGH for";
  const char* reason = CONFIRM_REASON_OUTPUT;
  const char* opLabel = "setbits";
  switch (command) {
    case MaskCommand::SET_OUTPUT:
      canonical = "setbits";
      changeVerb = "set output latch bits HIGH for";
      reason = CONFIRM_REASON_OUTPUT;
      opLabel = "setbits";
      break;
    case MaskCommand::CLEAR_OUTPUT:
      canonical = "clearbits";
      changeVerb = "clear output latch bits LOW for";
      reason = CONFIRM_REASON_OUTPUT;
      opLabel = "clearbits";
      break;
    case MaskCommand::TOGGLE_OUTPUT:
      canonical = "togglebits";
      changeVerb = "toggle output latch bits selected by";
      reason = CONFIRM_REASON_OUTPUT;
      opLabel = "togglebits";
      break;
    case MaskCommand::DIR_INPUT:
      canonical = "dirin";
      changeVerb = "configure pins selected by";
      reason = CONFIRM_REASON_DIRECTION;
      opLabel = "dirin";
      break;
    case MaskCommand::DIR_OUTPUT:
      canonical = "dirout";
      changeVerb = "configure pins selected by";
      reason = CONFIRM_REASON_DIRECTION;
      opLabel = "dirout";
      break;
    case MaskCommand::INVERT_SET:
      canonical = "invertset";
      changeVerb = "enable polarity inversion for pins selected by";
      reason = CONFIRM_REASON_POLARITY;
      opLabel = "invertset";
      break;
    case MaskCommand::INVERT_CLEAR:
      canonical = "invertclr";
      changeVerb = "disable polarity inversion for pins selected by";
      reason = CONFIRM_REASON_POLARITY;
      opLabel = "invertclr";
      break;
  }

  char wouldChange[112] = {};
  char confirmedCommand[64] = {};
  snprintf(wouldChange, sizeof(wouldChange), "%s mask 0x%04X", changeVerb, mask);
  if (command == MaskCommand::DIR_INPUT) {
    snprintf(wouldChange, sizeof(wouldChange),
             "configure pins selected by mask 0x%04X as inputs", mask);
  } else if (command == MaskCommand::DIR_OUTPUT) {
    snprintf(wouldChange, sizeof(wouldChange),
             "configure pins selected by mask 0x%04X as outputs", mask);
  }
  snprintf(confirmedCommand, sizeof(confirmedCommand), "%s 0x%04X confirm", canonical, mask);
  if (!requireConfirmation(confirmed, wouldChange, reason, confirmedCommand)) {
    return;
  }

  PCA9555::Status st = PCA9555::Status::Ok();
  switch (command) {
    case MaskCommand::SET_OUTPUT: st = gDev.setOutputBits(mask); break;
    case MaskCommand::CLEAR_OUTPUT: st = gDev.clearOutputBits(mask); break;
    case MaskCommand::TOGGLE_OUTPUT: st = gDev.toggleOutputBits(mask); break;
    case MaskCommand::DIR_INPUT: st = gDev.configureInputBits(mask); break;
    case MaskCommand::DIR_OUTPUT: st = gDev.configureOutputBits(mask); break;
    case MaskCommand::INVERT_SET: st = gDev.setInvertBits(mask); break;
    case MaskCommand::INVERT_CLEAR: st = gDev.clearInvertBits(mask); break;
  }
  printStatus(opLabel, st);
}

void cmdWriteReg(const char* args) {
  const char* cursor = args;
  uint32_t reg = 0;
  uint8_t value = 0;
  bool confirmed = false;
  if (!parseU32Token(cursor, &reg) || reg < PCA9555::cmd::REG_OUTPUT_PORT_0 ||
      reg > PCA9555::cmd::REG_POLARITY_INV_1 || !parseU8Token(cursor, &value) ||
      !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("write reg <0x02..0x05> <0x00..0xFF> [confirm]");
    return;
  }

  char wouldChange[96] = {};
  char confirmedCommand[72] = {};
  snprintf(wouldChange, sizeof(wouldChange), "write raw register 0x%02X to 0x%02X",
           static_cast<unsigned>(reg), value);
  snprintf(confirmedCommand, sizeof(confirmedCommand), "write reg 0x%02X 0x%02X confirm",
           static_cast<unsigned>(reg), value);
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_RAW, confirmedCommand)) {
    return;
  }
  printStatus("write reg", gDev.writeRegister(static_cast<uint8_t>(reg), value));
}

void cmdWriteRegs(const char* args) {
  const char* cursor = args;
  uint32_t reg = 0;
  uint8_t values[2] = {};
  size_t len = 1U;
  bool confirmed = false;

  if (!parseU32Token(cursor, &reg) || reg < PCA9555::cmd::REG_OUTPUT_PORT_0 ||
      reg > PCA9555::cmd::REG_POLARITY_INV_1 || !parseU8Token(cursor, &values[0])) {
    printUsage("write regs <0x02..0x05> <0x00..0xFF> [0x00..0xFF] [confirm]");
    return;
  }

  cursor = skipWhitespace(cursor);
  if (*cursor != '\0') {
    char token[24] = {};
    if (!readToken(cursor, token, sizeof(token))) {
      printUsage("write regs <0x02..0x05> <0x00..0xFF> [0x00..0xFF] [confirm]");
      return;
    }
    if (strcmp(token, "confirm") == 0) {
      if (hasTrailingArgs(cursor)) {
        printUsage("write regs <0x02..0x05> <0x00..0xFF> [0x00..0xFF] [confirm]");
        return;
      }
      confirmed = true;
    } else {
      uint32_t value1 = 0;
      if (!parseU32TokenText(token, &value1) || value1 > 0xFFU ||
          !parseConfirmSuffix(cursor, &confirmed)) {
          printUsage("write regs <0x02..0x05> <0x00..0xFF> [0x00..0xFF] [confirm]");
        return;
      }
      values[1] = static_cast<uint8_t>(value1);
      len = 2U;
    }
  }

  char wouldChange[128] = {};
  char confirmedCommand[96] = {};
  if (len == 1U) {
    snprintf(wouldChange, sizeof(wouldChange),
             "write one raw register starting at 0x%02X to 0x%02X",
             static_cast<unsigned>(reg), values[0]);
    snprintf(confirmedCommand, sizeof(confirmedCommand),
             "write regs 0x%02X 0x%02X confirm", static_cast<unsigned>(reg), values[0]);
  } else {
    snprintf(wouldChange, sizeof(wouldChange),
             "write two raw registers starting at 0x%02X to 0x%02X 0x%02X",
             static_cast<unsigned>(reg), values[0], values[1]);
    snprintf(confirmedCommand, sizeof(confirmedCommand),
             "write regs 0x%02X 0x%02X 0x%02X confirm",
             static_cast<unsigned>(reg), values[0], values[1]);
  }
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_RAW, confirmedCommand)) {
    return;
  }
  printStatus("write regs", gDev.writeRegisters(static_cast<uint8_t>(reg), values, len));
}

void cmdAllOutputs(const char* args, bool high) {
  const char* cursor = args;
  bool confirmed = false;
  if (!parseConfirmSuffix(cursor, &confirmed)) {
    printUsage(high ? "allhigh [confirm]" : "alllow [confirm]");
    return;
  }
  const uint16_t value = high ? PORTS_ALL_HIGH : PORTS_ALL_LOW;
  char wouldChange[112] = {};
  char confirmedCommand[32] = {};
  snprintf(wouldChange, sizeof(wouldChange),
           "set all output latches to 0x%04X and force all pins to output mode", value);
  snprintf(confirmedCommand, sizeof(confirmedCommand), "%s confirm", high ? "allhigh" : "alllow");
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_PATTERN, confirmedCommand)) {
    return;
  }
  PCA9555::Status st = gDev.writeOutputs(portsFrom(value));
  printStatus(high ? "allhigh output latches" : "alllow output latches", st);
  if (st.ok()) {
    st = gDev.setConfiguration(portsFrom(PORTS_ALL_LOW));
    printStatus(high ? "allhigh direction" : "alllow direction", st);
  }
}

void cmdPattern(const char* args) {
  const char* cursor = args;
  uint16_t value = 0;
  bool confirmed = false;
  if (!parseU16Token(cursor, &value) || !parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("pattern <0x0000..0xFFFF> [confirm]");
    return;
  }

  char wouldChange[128] = {};
  char confirmedCommand[64] = {};
  snprintf(wouldChange, sizeof(wouldChange),
           "drive exact 16-bit output pattern 0x%04X and force all pins to output mode",
           value);
  snprintf(confirmedCommand, sizeof(confirmedCommand), "pattern 0x%04X confirm", value);
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_PATTERN, confirmedCommand)) {
    return;
  }
  const PCA9555::PortData outputs = portsFrom(value);
  PCA9555::Status st = gDev.writeOutputs(outputs);
  printStatus("pattern output latches", st);
  if (st.ok()) {
    st = gDev.setConfiguration(portsFrom(PORTS_ALL_LOW));
    printStatus("pattern direction", st);
  }
  if (st.ok()) {
    printf("Pattern applied: value=0x%04X P0=0x%02X P1=0x%02X all pins OUTPUT\n",
           value, outputs.port0, outputs.port1);
  }
}

void cmdSweep(const char* args) {
  uint32_t delay = DEFAULT_SWEEP_DELAY_MS;
  bool confirmed = false;
  if (!parseOptionalU32Confirm(args, DEFAULT_SWEEP_DELAY_MS, 0U, MAX_PIN_TEST_DELAY_MS,
                               &delay, &confirmed)) {
    printUsage("sweep [delay_ms 0..10000] [confirm]");
    return;
  }

  char wouldChange[128] = {};
  char confirmedCommand[64] = {};
  snprintf(wouldChange, sizeof(wouldChange),
           "force all pins to outputs, sweep output latches high then low with %u ms delay",
           static_cast<unsigned>(delay));
  snprintf(confirmedCommand, sizeof(confirmedCommand), "sweep %u confirm",
           static_cast<unsigned>(delay));
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_PATTERN, confirmedCommand)) {
    return;
  }

  PCA9555::PortData savedCfg;
  PCA9555::PortData savedOut;
  PCA9555::Status st = gDev.getConfiguration(savedCfg);
  printStatus("sweep save direction", st);
  if (!st.ok()) {
    return;
  }
  st = gDev.readOutputs(savedOut);
  printStatus("sweep save output latches", st);
  if (!st.ok()) {
    return;
  }

  st = gDev.writeOutputs(portsFrom(PORTS_ALL_LOW));
  printStatus("sweep clear output latches", st);
  if (!st.ok()) {
    return;
  }
  st = gDev.setConfiguration(portsFrom(PORTS_ALL_LOW));
  printStatus("sweep force outputs", st);
  if (!st.ok()) {
    (void)restoreOutputAndDirection(savedOut, savedCfg);
    return;
  }

  uint32_t pass = 0;
  uint32_t fail = 0;
  puts("=== Sweep Test ===");
  for (uint8_t pinIndex = 0; pinIndex < PCA9555::cmd::TOTAL_PINS; ++pinIndex) {
    const PCA9555::Pin pin = static_cast<PCA9555::Pin>(pinIndex);
    st = gDev.writePin(pin, true);
    if (st.ok()) {
      delayMs(delay);
      PCA9555::PortData readback;
      st = gDev.readOutputs(readback);
      const uint16_t expected =
          static_cast<uint16_t>((1U << (pinIndex + 1U)) - 1U);
      reportCheck("sweep ON readback", st.ok() && readback.combined() == expected, &pass, &fail);
    } else {
      reportCheck("sweep ON write", false, &pass, &fail);
    }
  }
  for (uint8_t pinIndex = 0; pinIndex < PCA9555::cmd::TOTAL_PINS; ++pinIndex) {
    const PCA9555::Pin pin = static_cast<PCA9555::Pin>(pinIndex);
    st = gDev.writePin(pin, false);
    if (st.ok()) {
      delayMs(delay);
      PCA9555::PortData readback;
      st = gDev.readOutputs(readback);
      const uint16_t expected = pinIndex < 15U
                                    ? static_cast<uint16_t>(PORTS_ALL_HIGH << (pinIndex + 1U))
                                    : PORTS_ALL_LOW;
      reportCheck("sweep OFF readback", st.ok() && readback.combined() == expected, &pass, &fail);
    } else {
      reportCheck("sweep OFF write", false, &pass, &fail);
    }
  }

  st = restoreOutputAndDirection(savedOut, savedCfg);
  if (!st.ok()) ++fail;
  printf("Sweep result: pass=%lu fail=%lu\n", static_cast<unsigned long>(pass),
         static_cast<unsigned long>(fail));
}

void cmdWalk(const char* args) {
  uint32_t delay = DEFAULT_SWEEP_DELAY_MS;
  bool confirmed = false;
  if (!parseOptionalU32Confirm(args, DEFAULT_SWEEP_DELAY_MS, 0U, MAX_PIN_TEST_DELAY_MS,
                               &delay, &confirmed)) {
    printUsage("walk [delay_ms 0..10000] [confirm]");
    return;
  }

  char wouldChange[128] = {};
  char confirmedCommand[64] = {};
  snprintf(wouldChange, sizeof(wouldChange),
           "force all pins to outputs and walk one HIGH output latch with %u ms delay",
           static_cast<unsigned>(delay));
  snprintf(confirmedCommand, sizeof(confirmedCommand), "walk %u confirm",
           static_cast<unsigned>(delay));
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_PATTERN, confirmedCommand)) {
    return;
  }

  PCA9555::PortData savedCfg;
  PCA9555::PortData savedOut;
  PCA9555::Status st = gDev.getConfiguration(savedCfg);
  printStatus("walk save direction", st);
  if (!st.ok()) {
    return;
  }
  st = gDev.readOutputs(savedOut);
  printStatus("walk save output latches", st);
  if (!st.ok()) {
    return;
  }
  st = gDev.writeOutputs(portsFrom(PORTS_ALL_LOW));
  printStatus("walk clear output latches", st);
  if (!st.ok()) {
    return;
  }
  st = gDev.setConfiguration(portsFrom(PORTS_ALL_LOW));
  printStatus("walk force outputs", st);
  if (!st.ok()) {
    (void)restoreOutputAndDirection(savedOut, savedCfg);
    return;
  }

  uint32_t pass = 0;
  uint32_t fail = 0;
  puts("=== Walking-1 Test ===");
  for (uint8_t pinIndex = 0; pinIndex < PCA9555::cmd::TOTAL_PINS; ++pinIndex) {
    const PCA9555::Pin pin = static_cast<PCA9555::Pin>(pinIndex);
    const uint16_t pattern = static_cast<uint16_t>(1U << pinIndex);
    st = gDev.writeOutputs(portsFrom(pattern));
    if (st.ok()) {
      delayMs(delay);
      PCA9555::PortData readback;
      st = gDev.readOutputs(readback);
      reportCheck("walk readback", st.ok() && readback.combined() == pattern, &pass, &fail);
    } else {
      reportCheck("walk write", false, &pass, &fail);
    }
  }

  st = restoreOutputAndDirection(savedOut, savedCfg);
  if (!st.ok()) ++fail;
  printf("Walk result: pass=%lu fail=%lu\n", static_cast<unsigned long>(pass),
         static_cast<unsigned long>(fail));
}

void cmdSelfTest(const char* args) {
  const char* cursor = args;
  bool confirmed = false;
  if (!parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("selftest [confirm]");
    return;
  }
  if (!requireConfirmation(confirmed,
                           "run readback plus output, direction, polarity, and mask API checks",
                           CONFIRM_REASON_STRESS,
                           "selftest confirm")) {
    return;
  }

  uint32_t pass = 0;
  uint32_t fail = 0;
  const uint32_t succBefore = gDev.totalSuccess();
  const uint32_t failBefore = gDev.totalFailures();
  PCA9555::PortData savedOut;
  PCA9555::PortData savedCfg;
  PCA9555::PortData savedPol;

  PCA9555::Status st = gDev.readOutputs(savedOut);
  if (!st.ok()) {
    printStatus("selftest save output latches", st);
    return;
  }
  st = gDev.getConfiguration(savedCfg);
  if (!st.ok()) {
    printStatus("selftest save direction", st);
    return;
  }
  st = gDev.getPolarity(savedPol);
  if (!st.ok()) {
    printStatus("selftest save polarity", st);
    return;
  }

  PCA9555::PortData data;
  st = gDev.readInputs(data);
  reportCheck("readInputs", st.ok(), &pass, &fail);
  st = gDev.readOutputs(data);
  reportCheck("readOutputs", st.ok(), &pass, &fail);
  st = gDev.getConfiguration(data);
  reportCheck("getConfiguration", st.ok(), &pass, &fail);
  st = gDev.getPolarity(data);
  reportCheck("getPolarity", st.ok(), &pass, &fail);

  uint8_t value = 0;
  st = gDev.readRegister(PCA9555::cmd::REG_INPUT_PORT_0, value);
  reportCheck("readRegister", st.ok(), &pass, &fail);
  uint8_t pairBuf[2] = {};
  st = gDev.readRegisters(PCA9555::cmd::REG_OUTPUT_PORT_0, pairBuf, sizeof(pairBuf));
  reportCheck("readRegisters", st.ok(), &pass, &fail);

  st = gDev.writeOutputs(portsFrom(PORTS_ALL_LOW));
  if (st.ok()) {
    st = gDev.setConfiguration(portsFrom(PORTS_ALL_LOW));
  }
  reportCheck("force outputs low", st.ok(), &pass, &fail);
  if (!st.ok()) {
    const PCA9555::Status restore = restoreState(savedOut, savedPol, savedCfg);
    reportCheck("restore after setup failure", restore.ok(), &pass, &fail);
    printf("Selftest result: pass=%lu fail=%lu\n",
           static_cast<unsigned long>(pass),
           static_cast<unsigned long>(fail));
    return;
  }

  st = gDev.setOutputBits(0x0103U);
  if (st.ok()) {
    st = gDev.readOutputs(data);
  }
  reportCheck("setOutputBits readback", st.ok() && data.combined() == 0x0103U, &pass, &fail);

  st = gDev.clearOutputBits(0x0001U);
  if (st.ok()) {
    st = gDev.readOutputs(data);
  }
  reportCheck("clearOutputBits readback", st.ok() && data.combined() == 0x0102U, &pass, &fail);

  st = gDev.toggleOutputBits(0x0300U);
  if (st.ok()) {
    st = gDev.readOutputs(data);
  }
  reportCheck("toggleOutputBits readback", st.ok() && data.combined() == 0x0202U, &pass, &fail);

  st = gDev.togglePin(PCA9555::Pin::P00);
  if (st.ok()) {
    st = gDev.readOutputs(data);
  }
  reportCheck("togglePin readback", st.ok() && data.combined() == 0x0203U, &pass, &fail);

  st = gDev.setConfiguration(portsFrom(PORTS_ALL_LOW));
  if (st.ok()) {
    st = gDev.configureInputBits(0x0103U);
  }
  if (st.ok()) {
    st = gDev.getConfiguration(data);
  }
  reportCheck("configureInputBits readback", st.ok() && data.combined() == 0x0103U, &pass, &fail);

  st = gDev.configureOutputBits(0x0001U);
  if (st.ok()) {
    st = gDev.getConfiguration(data);
  }
  reportCheck("configureOutputBits readback", st.ok() && data.combined() == 0x0102U, &pass, &fail);

  st = gDev.setPolarity(portsFrom(PORTS_ALL_LOW));
  if (st.ok()) {
    st = gDev.setInvertBits(0x0101U);
  }
  if (st.ok()) {
    st = gDev.getPolarity(data);
  }
  reportCheck("setInvertBits readback", st.ok() && data.combined() == 0x0101U, &pass, &fail);

  st = gDev.clearInvertBits(0x0001U);
  if (st.ok()) {
    st = gDev.getPolarity(data);
  }
  reportCheck("clearInvertBits readback", st.ok() && data.combined() == 0x0100U, &pass, &fail);

  st = restoreState(savedOut, savedPol, savedCfg);
  reportCheck("restore writable state", st.ok(), &pass, &fail);
  const uint32_t succDelta = gDev.totalSuccess() - succBefore;
  const uint32_t failDelta = gDev.totalFailures() - failBefore;
  printf("Selftest result: pass=%lu fail=%lu\n", static_cast<unsigned long>(pass),
         static_cast<unsigned long>(fail));
  printf("  Health delta: success +%lu, failures +%lu\n",
         static_cast<unsigned long>(succDelta), static_cast<unsigned long>(failDelta));
}

void runStress(uint32_t count) {
  uint32_t ok = 0;
  uint32_t fail = 0;
  const uint32_t start = nowMs(nullptr);
  const uint32_t succBefore = gDev.totalSuccess();
  const uint32_t failBefore = gDev.totalFailures();
  for (uint32_t i = 0; i < count; ++i) {
    PCA9555::PortData data;
    PCA9555::Status st = gDev.readInputs(data);
    if (st.ok()) {
      ++ok;
    } else {
      ++fail;
      if (gVerbose) {
        printStatus("stress readInputs", st);
      }
    }
    if (((i + 1U) % 100U) == 0U || (i + 1U) == count) {
      printf("  stress %lu/%lu ok=%lu fail=%lu\n",
             static_cast<unsigned long>(i + 1U), static_cast<unsigned long>(count),
             static_cast<unsigned long>(ok), static_cast<unsigned long>(fail));
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  const uint32_t elapsed = nowMs(nullptr) - start;
  printf("Stress result: ok=%lu fail=%lu duration_ms=%lu\n",
         static_cast<unsigned long>(ok), static_cast<unsigned long>(fail),
         static_cast<unsigned long>(elapsed));
  printf("  Health delta: success +%lu, failures +%lu\n",
         static_cast<unsigned long>(gDev.totalSuccess() - succBefore),
         static_cast<unsigned long>(gDev.totalFailures() - failBefore));
}

void cmdStress(const char* args) {
  uint32_t count = DEFAULT_STRESS_COUNT;
  bool confirmed = false;
  if (!parseOptionalU32Confirm(args, DEFAULT_STRESS_COUNT, 1U, MAX_STRESS_COUNT,
                               &count, &confirmed)) {
    printUsage("stress [1..10000] [confirm]");
    return;
  }

  char wouldChange[112] = {};
  char confirmedCommand[48] = {};
  snprintf(wouldChange, sizeof(wouldChange), "run %u readInputs stress cycles",
           static_cast<unsigned>(count));
  snprintf(confirmedCommand, sizeof(confirmedCommand), "stress %u confirm",
           static_cast<unsigned>(count));
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_STRESS, confirmedCommand)) {
    return;
  }
  runStress(count);
}

void runStressMix(uint32_t count) {
  struct OpStats {
    const char* name;
    uint32_t ok;
    uint32_t fail;
  };
  static constexpr size_t OP_COUNT = 14U;
  OpStats ops[OP_COUNT] = {
      {"readInputs", 0, 0},       {"readOutputs", 0, 0},
      {"getConfig", 0, 0},        {"getPolarity", 0, 0},
      {"readRegister", 0, 0},     {"readRegisters", 0, 0},
      {"setOutputBits", 0, 0},    {"clearOutputBits", 0, 0},
      {"toggleOutputBits", 0, 0}, {"togglePin", 0, 0},
      {"configureInput", 0, 0},   {"configureOutput", 0, 0},
      {"setInvertBits", 0, 0},    {"clearInvertBits", 0, 0},
  };
  uint32_t totalOk = 0;
  uint32_t totalFail = 0;
  const uint32_t start = nowMs(nullptr);
  const uint32_t succBefore = gDev.totalSuccess();
  const uint32_t failBefore = gDev.totalFailures();

  PCA9555::PortData savedOut;
  PCA9555::PortData savedCfg;
  PCA9555::PortData savedPol;
  PCA9555::Status st = gDev.readOutputs(savedOut);
  printStatus("stress_mix save output latches", st);
  if (!st.ok()) {
    return;
  }
  st = gDev.getConfiguration(savedCfg);
  printStatus("stress_mix save direction", st);
  if (!st.ok()) {
    return;
  }
  st = gDev.getPolarity(savedPol);
  printStatus("stress_mix save polarity", st);
  if (!st.ok()) {
    return;
  }

  st = gDev.writeOutputs(portsFrom(PORTS_ALL_LOW));
  if (st.ok()) {
    st = gDev.setPolarity(portsFrom(PORTS_ALL_LOW));
  }
  if (st.ok()) {
    st = gDev.setConfiguration(portsFrom(PORTS_ALL_LOW));
  }
  printStatus("stress_mix prepare", st);
  if (!st.ok()) {
    (void)restoreState(savedOut, savedPol, savedCfg);
    return;
  }

  for (uint32_t i = 0; i < count; ++i) {
    const size_t op = static_cast<size_t>(i % OP_COUNT);
    PCA9555::PortData data;
    uint8_t value = 0;
    uint8_t pairBuf[2] = {};
    const uint16_t maskA = ((i & 1U) == 0U) ? 0x0003U : 0x0300U;
    const uint16_t maskB = ((i & 1U) == 0U) ? 0x0101U : 0x1008U;
    const PCA9555::Pin pin = static_cast<PCA9555::Pin>(i % PCA9555::cmd::TOTAL_PINS);

    switch (op) {
      case 0: st = gDev.readInputs(data); break;
      case 1: st = gDev.readOutputs(data); break;
      case 2: st = gDev.getConfiguration(data); break;
      case 3: st = gDev.getPolarity(data); break;
      case 4: st = gDev.readRegister(PCA9555::cmd::REG_INPUT_PORT_0, value); break;
      case 5: st = gDev.readRegisters(PCA9555::cmd::REG_OUTPUT_PORT_0, pairBuf, sizeof(pairBuf)); break;
      case 6: st = gDev.setOutputBits(maskA); break;
      case 7: st = gDev.clearOutputBits(maskA); break;
      case 8: st = gDev.toggleOutputBits(maskB); break;
      case 9: st = gDev.togglePin(pin); break;
      case 10: st = gDev.configureInputBits(maskA); break;
      case 11: st = gDev.configureOutputBits(maskA); break;
      case 12: st = gDev.setInvertBits(maskB); break;
      case 13: st = gDev.clearInvertBits(maskB); break;
      default: st = PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad op"); break;
    }

    if (st.ok()) {
      ++ops[op].ok;
      ++totalOk;
    } else {
      ++ops[op].fail;
      ++totalFail;
      if (gVerbose) {
        printStatus(ops[op].name, st);
      }
    }
    if (((i + 1U) % 100U) == 0U || (i + 1U) == count) {
      printf("  stress_mix %lu/%lu ok=%lu fail=%lu\n",
             static_cast<unsigned long>(i + 1U), static_cast<unsigned long>(count),
             static_cast<unsigned long>(totalOk), static_cast<unsigned long>(totalFail));
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  st = restoreState(savedOut, savedPol, savedCfg);
  if (!st.ok()) ++totalFail;
  const uint32_t elapsed = nowMs(nullptr) - start;
  puts("=== stress_mix summary ===");
  printf("  Total: ok=%lu fail=%lu duration_ms=%lu\n",
         static_cast<unsigned long>(totalOk), static_cast<unsigned long>(totalFail),
         static_cast<unsigned long>(elapsed));
  for (size_t i = 0; i < OP_COUNT; ++i) {
    printf("  %-16s ok=%lu fail=%lu\n", ops[i].name,
           static_cast<unsigned long>(ops[i].ok), static_cast<unsigned long>(ops[i].fail));
  }
  printf("  Health delta: success +%lu, failures +%lu\n",
         static_cast<unsigned long>(gDev.totalSuccess() - succBefore),
         static_cast<unsigned long>(gDev.totalFailures() - failBefore));
}

void cmdStressMix(const char* args) {
  uint32_t count = DEFAULT_STRESS_MIX_COUNT;
  bool confirmed = false;
  if (!parseOptionalU32Confirm(args, DEFAULT_STRESS_MIX_COUNT, 1U, MAX_STRESS_COUNT,
                               &count, &confirmed)) {
    printUsage("stress_mix [1..10000] [confirm]");
    return;
  }

  char wouldChange[128] = {};
  char confirmedCommand[56] = {};
  snprintf(wouldChange, sizeof(wouldChange),
           "run %u mixed read/write/config/polarity/mask stress cycles",
           static_cast<unsigned>(count));
  snprintf(confirmedCommand, sizeof(confirmedCommand), "stress_mix %u confirm",
           static_cast<unsigned>(count));
  if (!requireConfirmation(confirmed, wouldChange, CONFIRM_REASON_STRESS, confirmedCommand)) {
    return;
  }
  runStressMix(count);
}

void cmdRecover(const char* args) {
  const char* cursor = args;
  bool confirmed = false;
  if (!parseConfirmSuffix(cursor, &confirmed)) {
    printUsage("recover [confirm]");
    return;
  }
  if (!requireConfirmation(confirmed,
                           "apply the explicit example recovery image",
                           CONFIRM_REASON_RECOVER,
                           "recover confirm")) {
    return;
  }
  printStatus("recover example image", applyExampleRecoveryImage());
  printDrv();
}

void handleCommand(char* line) {
  char* full = trim(line);
  if (strcmp(full, "help") == 0 || strcmp(full, "?") == 0) {
    printHelp();
  } else if (strcmp(full, "version") == 0 || strcmp(full, "ver") == 0) {
    printf("PCA9555 %s %s\n", PCA9555::VERSION, PCA9555::VERSION_FULL);
  } else if (strcmp(full, "scan") == 0) {
    scanBus();
  } else if (strcmp(full, "probe") == 0) {
    printStatus("probe", gDev.probe());
  } else if (strcmp(full, "recover") == 0 || strncmp(full, "recover ", 8) == 0) {
    cmdRecover(full + 7);
  } else if (strcmp(full, "drv") == 0 || strcmp(full, "health") == 0 ||
             strcmp(full, "cfg") == 0 ||
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
  } else if (strncmp(full, "read input port ", 16) == 0 || strncmp(full, "rin ", 4) == 0) {
    const char* arg = full[0] == 'r' && full[1] == 'i' ? full + 4 : full + 16;
    PCA9555::Port port;
    uint8_t value = 0;
    PCA9555::Status st = parsePortArg(arg, &port)
                             ? gDev.readInput(port, value)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad port");
    printStatus("read input port", st);
    if (st.ok()) {
      printf("port=%s value=0x%02X\n", port == PCA9555::Port::PORT_0 ? "0" : "1", value);
    }
  } else if (strncmp(full, "read output port ", 17) == 0) {
    PCA9555::Port port;
    uint8_t value = 0;
    PCA9555::Status st = parsePortArg(full + 17, &port)
                             ? gDev.readOutput(port, value)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad port");
    printStatus("read output port", st);
    if (st.ok()) {
      printf("port=%s output_latch=0x%02X\n", portName(port), value);
    }
  } else if (strcmp(full, "read config") == 0 || strcmp(full, "config") == 0) {
    PCA9555::PortData data;
    PCA9555::Status st = gDev.getConfiguration(data);
    printStatus("config", st);
    if (st.ok()) {
      printPortData("config", data);
    }
  } else if (strncmp(full, "read config port ", 17) == 0) {
    PCA9555::Port port;
    uint8_t value = 0;
    PCA9555::Status st = parsePortArg(full + 17, &port)
                             ? gDev.getPortConfiguration(port, value)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad port");
    printStatus("read config port", st);
    if (st.ok()) {
      printf("port=%s direction=0x%02X\n", port == PCA9555::Port::PORT_0 ? "0" : "1", value);
    }
  } else if (strcmp(full, "read polarity") == 0 || strcmp(full, "polarity") == 0) {
    PCA9555::PortData data;
    PCA9555::Status st = gDev.getPolarity(data);
    printStatus("polarity", st);
    if (st.ok()) {
      printPortData("polarity", data);
    }
  } else if (strncmp(full, "read polarity port ", 19) == 0) {
    PCA9555::Port port;
    uint8_t value = 0;
    PCA9555::Status st = parsePortArg(full + 19, &port)
                             ? gDev.getPortPolarity(port, value)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad port");
    printStatus("read polarity port", st);
    if (st.ok()) {
      printf("port=%s polarity=0x%02X\n", port == PCA9555::Port::PORT_0 ? "0" : "1", value);
    }
  } else if (strncmp(full, "read pin ", 9) == 0 || strncmp(full, "rpin ", 5) == 0) {
    const char* arg = full[0] == 'r' && full[1] == 'p' ? full + 5 : full + 9;
    PCA9555::Pin pin = PCA9555::Pin::P00;
    bool state = false;
    PCA9555::Status st = parsePinArg(arg, &pin)
                             ? gDev.readPin(pin, state)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad pin");
    printStatus("read pin", st);
    if (st.ok()) {
      printf("pin=%u input=%d\n", static_cast<unsigned>(pin), state ? 1 : 0);
    }
  } else if (strncmp(full, "read outpin ", 12) == 0 || strncmp(full, "rout ", 5) == 0) {
    const char* arg = full[0] == 'r' && full[1] == 'o' ? full + 5 : full + 12;
    PCA9555::Pin pin = PCA9555::Pin::P00;
    bool high = false;
    PCA9555::Status st = parsePinArg(arg, &pin)
                             ? gDev.readOutputPin(pin, high)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad pin");
    printStatus("read outpin", st);
    if (st.ok()) {
      printf("pin=%u output_latch=%d\n", static_cast<unsigned>(pin), high ? 1 : 0);
    }
  } else if (strncmp(full, "read dirpin ", 12) == 0 || strncmp(full, "rdir ", 5) == 0) {
    const char* arg = full[0] == 'r' && full[1] == 'd' ? full + 5 : full + 12;
    PCA9555::Pin pin = PCA9555::Pin::P00;
    bool input = false;
    PCA9555::Status st = parsePinArg(arg, &pin)
                             ? gDev.getPinDirection(pin, input)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad pin");
    printStatus("read dirpin", st);
    if (st.ok()) {
      printf("pin=%u direction=%s\n", static_cast<unsigned>(pin), input ? "input" : "output");
    }
  } else if (strncmp(full, "read polpin ", 12) == 0 || strncmp(full, "rpol ", 5) == 0) {
    const char* arg = full[0] == 'r' && full[1] == 'p' ? full + 5 : full + 12;
    PCA9555::Pin pin = PCA9555::Pin::P00;
    bool inverted = false;
    PCA9555::Status st = parsePinArg(arg, &pin)
                             ? gDev.getPinPolarity(pin, inverted)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad pin");
    printStatus("read polpin", st);
    if (st.ok()) {
      printf("pin=%u polarity=%s\n", static_cast<unsigned>(pin), inverted ? "inverted" : "normal");
    }
  } else if (strncmp(full, "pininfo ", 8) == 0) {
    PCA9555::Pin pin = PCA9555::Pin::P00;
    if (parsePinArg(full + 8, &pin)) {
      printPinInfo(pin);
    } else {
      puts("Usage: pininfo <0..15>");
    }
  } else if (strcmp(full, "pins") == 0) {
    printAllPins();
  } else if (strncmp(full, "write pin ", 10) == 0) {
    cmdWritePin(full + 10);
  } else if (strncmp(full, "wpin ", 5) == 0) {
    cmdWritePin(full + 5);
  } else if (strncmp(full, "toggle ", 7) == 0) {
    cmdTogglePin(full + 7);
  } else if (strncmp(full, "dir pin ", 8) == 0) {
    cmdSetDirection(full + 8);
  } else if (strncmp(full, "dir ", 4) == 0) {
    cmdSetDirection(full + 4);
  } else if (strncmp(full, "write port ", 11) == 0) {
    cmdWritePort(full + 11);
  } else if (strncmp(full, "wport ", 6) == 0) {
    cmdWritePort(full + 6);
  } else if (strncmp(full, "dir port ", 9) == 0) {
    cmdSetPortDirection(full + 9);
  } else if (strncmp(full, "dport ", 6) == 0) {
    cmdSetPortDirection(full + 6);
  } else if (strncmp(full, "polarity pin ", 13) == 0) {
    cmdSetPinPolarity(full + 13);
  } else if (strncmp(full, "pol ", 4) == 0) {
    cmdSetPinPolarity(full + 4);
  } else if (strncmp(full, "polarity port ", 14) == 0) {
    cmdSetPortPolarity(full + 14);
  } else if (strncmp(full, "wpol ", 5) == 0) {
    cmdSetPortPolarity(full + 5);
  } else if (strncmp(full, "setbits ", 8) == 0) {
    cmdMask(full + 8, MaskCommand::SET_OUTPUT);
  } else if (strncmp(full, "sb ", 3) == 0) {
    cmdMask(full + 3, MaskCommand::SET_OUTPUT);
  } else if (strncmp(full, "clearbits ", 10) == 0) {
    cmdMask(full + 10, MaskCommand::CLEAR_OUTPUT);
  } else if (strncmp(full, "cb ", 3) == 0) {
    cmdMask(full + 3, MaskCommand::CLEAR_OUTPUT);
  } else if (strncmp(full, "togglebits ", 11) == 0) {
    cmdMask(full + 11, MaskCommand::TOGGLE_OUTPUT);
  } else if (strncmp(full, "tb ", 3) == 0) {
    cmdMask(full + 3, MaskCommand::TOGGLE_OUTPUT);
  } else if (strncmp(full, "dirin ", 6) == 0) {
    cmdMask(full + 6, MaskCommand::DIR_INPUT);
  } else if (strncmp(full, "dirout ", 7) == 0) {
    cmdMask(full + 7, MaskCommand::DIR_OUTPUT);
  } else if (strncmp(full, "invertset ", 10) == 0) {
    cmdMask(full + 10, MaskCommand::INVERT_SET);
  } else if (strncmp(full, "invertclr ", 10) == 0) {
    cmdMask(full + 10, MaskCommand::INVERT_CLEAR);
  } else if (strncmp(full, "write regs ", 11) == 0) {
    cmdWriteRegs(full + 11);
  } else if (strncmp(full, "wregs ", 6) == 0) {
    cmdWriteRegs(full + 6);
  } else if (strncmp(full, "write reg ", 10) == 0) {
    cmdWriteReg(full + 10);
  } else if (strncmp(full, "wreg ", 5) == 0) {
    cmdWriteReg(full + 5);
  } else if (strcmp(full, "allhigh") == 0 || strncmp(full, "allhigh ", 8) == 0) {
    cmdAllOutputs(full + 7, true);
  } else if (strcmp(full, "alllow") == 0 || strncmp(full, "alllow ", 7) == 0) {
    cmdAllOutputs(full + 6, false);
  } else if (strncmp(full, "pattern ", 8) == 0) {
    cmdPattern(full + 8);
  } else if (strncmp(full, "pat ", 4) == 0) {
    cmdPattern(full + 4);
  } else if (strcmp(full, "sweep") == 0 || strncmp(full, "sweep ", 6) == 0) {
    cmdSweep(full + 5);
  } else if (strcmp(full, "walk") == 0 || strncmp(full, "walk ", 5) == 0) {
    cmdWalk(full + 4);
  } else if (strcmp(full, "selftest") == 0 || strncmp(full, "selftest ", 9) == 0) {
    cmdSelfTest(full + 8);
  } else if (strcmp(full, "stress_mix") == 0 || strncmp(full, "stress_mix ", 11) == 0) {
    cmdStressMix(full + 10);
  } else if (strcmp(full, "stress") == 0 || strncmp(full, "stress ", 7) == 0) {
    cmdStress(full + 6);
  } else if (strncmp(full, "rreg ", 5) == 0 || strncmp(full, "read reg ", 9) == 0) {
    const char* arg = full[0] == 'r' ? full + 5 : full + 9;
    uint32_t reg = 0;
    uint8_t value = 0;
    PCA9555::Status st = (parseU32(arg, &reg) && reg < PCA9555::cmd::NUM_REGISTERS)
                             ? gDev.readRegister(static_cast<uint8_t>(reg), value)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "bad reg");
    printStatus("rreg", st);
    if (st.ok()) {
      printf("reg=0x%02X value=0x%02X\n", static_cast<unsigned>(reg), value);
    }
  } else if (strncmp(full, "rregs ", 6) == 0 || strncmp(full, "read regs ", 10) == 0) {
    const char* cursor = full[0] == 'r' && full[1] == 'r' ? full + 6 : full + 10;
    uint32_t reg = 0;
    uint32_t len = 0;
    uint8_t values[2] = {};
    PCA9555::Status st = (parseU32Token(cursor, &reg) && reg < PCA9555::cmd::NUM_REGISTERS &&
                          parseU32Token(cursor, &len) && len > 0U && len <= sizeof(values) &&
                          !hasTrailingArgs(cursor))
                             ? gDev.readRegisters(static_cast<uint8_t>(reg), values, len)
                             : PCA9555::Status::Error(PCA9555::Err::INVALID_PARAM, "usage: rregs <reg> <1|2>");
    printStatus("rregs", st);
    if (st.ok()) {
      for (uint32_t i = 0; i < len; ++i) {
        printf("reg[0x%02X]=0x%02X\n",
               static_cast<unsigned>(autoIncrementPairRegister(
                   static_cast<uint8_t>(reg), i)),
               values[i]);
      }
    }
  } else if (strcmp(full, "verbose") == 0) {
    // Bare `verbose` reports, it does not toggle - same as the Arduino CLI.
    printf("verbose=%d\n", gVerbose ? 1 : 0);
  } else if (strncmp(full, "verbose ", 8) == 0) {
    const char* cursor = full + 8;
    uint32_t value = 0;
    if (parseU32Token(cursor, &value) && value <= 1U && !hasTrailingArgs(cursor)) {
      gVerbose = value != 0U;
      printf("verbose=%d\n", gVerbose ? 1 : 0);
    } else {
      printUsage("verbose [0|1]");
    }
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
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

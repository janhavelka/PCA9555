/// @file main.cpp
/// @brief Basic bringup example for PCA9555 16-bit I/O expander
/// @note This is an EXAMPLE, not part of the library

#include <Arduino.h>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "examples/common/Log.h"
#include "examples/common/BoardConfig.h"
#include "examples/common/BusDiag.h"
#include "examples/common/CliShell.h"
#include "examples/common/HealthDiag.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/I2cScanner.h"

#include "PCA9555/PCA9555.h"

// ============================================================================
// Globals
// ============================================================================

PCA9555::PCA9555 device;
bool verboseMode = false;

// Stress test state (non-blocking)
struct StressStats {
  bool active = false;
  int total = 0;
  int okCount = 0;
  int failCount = 0;
  uint32_t startMs = 0;
  bool hasFailure = false;
  PCA9555::Status firstFailure;
  PCA9555::Status lastFailure;
  uint32_t succBefore = 0;
  uint32_t failBefore = 0;
};
StressStats stressStats;
int stressRemaining = 0;

// ============================================================================
// Helper Functions
// ============================================================================

uint32_t exampleNowMs(void*) {
  return millis();
}

const char* errToStr(PCA9555::Err err) {
  using namespace PCA9555;
  switch (err) {
    case Err::OK:                  return "OK";
    case Err::NOT_INITIALIZED:     return "NOT_INITIALIZED";
    case Err::INVALID_CONFIG:      return "INVALID_CONFIG";
    case Err::I2C_ERROR:           return "I2C_ERROR";
    case Err::TIMEOUT:             return "TIMEOUT";
    case Err::INVALID_PARAM:       return "INVALID_PARAM";
    case Err::DEVICE_NOT_FOUND:    return "DEVICE_NOT_FOUND";
    case Err::CONFIG_REG_MISMATCH: return "CONFIG_REG_MISMATCH";
    case Err::BUSY:                return "BUSY";
    case Err::IN_PROGRESS:         return "IN_PROGRESS";
    case Err::I2C_NACK_ADDR:      return "I2C_NACK_ADDR";
    case Err::I2C_NACK_DATA:      return "I2C_NACK_DATA";
    case Err::I2C_TIMEOUT:        return "I2C_TIMEOUT";
    case Err::I2C_BUS:            return "I2C_BUS";
    default:                       return "UNKNOWN";
  }
}

const char* stateToStr(PCA9555::DriverState st) {
  using namespace PCA9555;
  switch (st) {
    case DriverState::UNINIT:   return "UNINIT";
    case DriverState::READY:    return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE:  return "OFFLINE";
    default:                    return "UNKNOWN";
  }
}

const char* stateColor(PCA9555::DriverState st, bool online, uint8_t consecutiveFailures) {
  if (st == PCA9555::DriverState::UNINIT) {
    return LOG_COLOR_RESET;
  }
  return LOG_COLOR_STATE(online, consecutiveFailures);
}

const char* goodIfZeroColor(uint32_t value) {
  return (value == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

const char* goodIfNonZeroColor(uint32_t value) {
  return (value > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}

const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

const char* onOffColor(bool on) {
  return on ? LOG_COLOR_GREEN : LOG_COLOR_RESET;
}

const char* skipCountColor(uint32_t value) {
  return (value > 0U) ? LOG_COLOR_YELLOW : LOG_COLOR_RESET;
}

const char* skipWhitespace(const char* text) {
  while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text)) != 0) {
    ++text;
  }
  return text;
}

bool hasTrailingArgs(const char* text) {
  return *skipWhitespace(text) != '\0';
}

bool tokenEqualsIgnoreCase(const char* token, size_t len, const char* expected) {
  if (std::strlen(expected) != len) {
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    if (std::tolower(static_cast<unsigned char>(token[i])) !=
        std::tolower(static_cast<unsigned char>(expected[i]))) {
      return false;
    }
  }

  return true;
}

bool parseLongToken(const char*& text, long& value) {
  text = skipWhitespace(text);
  if (*text == '\0') {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const long parsed = strtol(text, &end, 0);
  if (end == text || errno == ERANGE) {
    return false;
  }

  text = skipWhitespace(end);
  value = parsed;
  return true;
}

bool parsePinToken(const char*& text, PCA9555::Pin& pin) {
  long value = 0;
  if (!parseLongToken(text, value) || value < 0 || value >= PCA9555::cmd::TOTAL_PINS) {
    return false;
  }

  pin = static_cast<PCA9555::Pin>(value);
  return true;
}

bool parsePortToken(const char*& text, PCA9555::Port& port) {
  long value = 0;
  if (!parseLongToken(text, value) || (value != 0 && value != 1)) {
    return false;
  }

  port = (value == 0) ? PCA9555::Port::PORT_0 : PCA9555::Port::PORT_1;
  return true;
}

bool parseByteToken(const char*& text, uint8_t& value) {
  long parsed = 0;
  if (!parseLongToken(text, parsed) || parsed < 0 || parsed > 0xFFL) {
    return false;
  }

  value = static_cast<uint8_t>(parsed);
  return true;
}

bool parseU16Token(const char*& text, uint16_t& value) {
  long parsed = 0;
  if (!parseLongToken(text, parsed) || parsed < 0 || parsed > 0xFFFFL) {
    return false;
  }

  value = static_cast<uint16_t>(parsed);
  return true;
}

bool parseBinaryToken(const char*& text, bool& value) {
  long parsed = 0;
  if (!parseLongToken(text, parsed) || (parsed != 0 && parsed != 1)) {
    return false;
  }

  value = (parsed != 0);
  return true;
}

bool parseDirectionToken(const char*& text, bool& input) {
  text = skipWhitespace(text);
  if (*text == '\0') {
    return false;
  }

  const char* start = text;
  while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text)) == 0) {
    ++text;
  }
  const size_t len = static_cast<size_t>(text - start);
  text = skipWhitespace(text);

  if (tokenEqualsIgnoreCase(start, len, "in")) {
    input = true;
    return true;
  }
  if (tokenEqualsIgnoreCase(start, len, "out")) {
    input = false;
    return true;
  }

  return false;
}

void printStatus(const PCA9555::Status& st) {
  Serial.printf("  Status: %s%s%s (code=%u, detail=%ld)\n",
                LOG_COLOR_RESULT(st.ok()),
                errToStr(st.code),
                LOG_COLOR_RESET,
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail));
  if (st.msg && st.msg[0]) {
    Serial.printf("  Message: %s%s%s\n", LOG_COLOR_YELLOW, st.msg, LOG_COLOR_RESET);
  }
}

void printVerboseState() {
  Serial.printf("  Verbose: %s%s%s\n",
                onOffColor(verboseMode),
                verboseMode ? "ON" : "OFF",
                LOG_COLOR_RESET);
}

void printDriverHealth() {
  health_diag::printHealthDiag(device.getSettings(), millis());
}

void printPortBinary(const char* label, uint8_t value) {
  Serial.printf("  %s: 0x%02X (", label, value);
  for (int i = 7; i >= 0; --i) {
    Serial.print((value >> i) & 1);
  }
  Serial.println(")");
}

// ============================================================================
// Version
// ============================================================================

void printVersionInfo() {
  Serial.println("=== Version Info ===");
  Serial.printf("  Example firmware build: %s %s\n", __DATE__, __TIME__);
  Serial.printf("  PCA9555 library version: %s\n", PCA9555::VERSION);
  Serial.printf("  PCA9555 library full: %s\n", PCA9555::VERSION_FULL);
  Serial.printf("  PCA9555 library build: %s\n", PCA9555::BUILD_TIMESTAMP);
  Serial.printf("  PCA9555 library commit: %s (%s)\n", PCA9555::GIT_COMMIT, PCA9555::GIT_STATUS);
}

void printSettings() {
  const PCA9555::SettingsSnapshot snapshot = device.getSettings();
  const PCA9555::Config& cfg = snapshot.config;

  Serial.println("=== Settings Snapshot ===");
  Serial.printf("  Initialized: %s%s%s\n",
                onOffColor(snapshot.initialized),
                snapshot.initialized ? "YES" : "NO",
                LOG_COLOR_RESET);
  Serial.printf("  State: %s%s%s\n",
                stateColor(snapshot.state,
                           snapshot.state == PCA9555::DriverState::READY ||
                               snapshot.state == PCA9555::DriverState::DEGRADED,
                           snapshot.consecutiveFailures),
                stateToStr(snapshot.state),
                LOG_COLOR_RESET);
  Serial.printf("  I2C address: 0x%02X\n", cfg.i2cAddress);
  Serial.printf("  Timeout: %lu ms\n", static_cast<unsigned long>(cfg.i2cTimeoutMs));
  Serial.printf("  Offline threshold: %u\n", cfg.offlineThreshold);
  Serial.printf("  Require POR config defaults: %s%s%s\n",
                onOffColor(cfg.requireConfigPortDefaults),
                cfg.requireConfigPortDefaults ? "YES" : "NO",
                LOG_COLOR_RESET);
  Serial.printf("  Interrupt errata workaround: %s%s%s\n",
                onOffColor(cfg.applyInterruptErrata),
                cfg.applyInterruptErrata ? "ENABLED" : "DISABLED",
                LOG_COLOR_RESET);
  Serial.printf("  nowMs hook: %s%s%s\n",
                onOffColor(cfg.nowMs != nullptr),
                (cfg.nowMs != nullptr) ? "SET" : "NONE",
                LOG_COLOR_RESET);
  printPortBinary("Desired Out P0", cfg.outputPort0);
  printPortBinary("Desired Out P1", cfg.outputPort1);
  printPortBinary("Desired Cfg P0", cfg.configPort0);
  printPortBinary("Desired Cfg P1", cfg.configPort1);
  printPortBinary("Desired Pol P0", cfg.polarityPort0);
  printPortBinary("Desired Pol P1", cfg.polarityPort1);
}

// ============================================================================
// Register Dump
// ============================================================================

static const char* regName(uint8_t reg) {
  switch (reg) {
    case 0: return "Input  Port 0";
    case 1: return "Input  Port 1";
    case 2: return "Output Port 0";
    case 3: return "Output Port 1";
    case 4: return "Polarity  P0 ";
    case 5: return "Polarity  P1 ";
    case 6: return "Config    P0 ";
    case 7: return "Config    P1 ";
    default: return "Unknown       ";
  }
}

void cmdDump() {
  Serial.println("=== Register Dump ===");
  for (uint8_t reg = 0; reg < 8; ++reg) {
    uint8_t val = 0;
    PCA9555::Status st = device.readRegister(reg, val);
    if (!st.ok()) {
      Serial.printf("  [0x%02X] %s: %sERROR%s (%s)\n",
                    reg, regName(reg), LOG_COLOR_RED, LOG_COLOR_RESET, errToStr(st.code));
    } else {
      Serial.printf("  [0x%02X] %s: 0x%02X (", reg, regName(reg), val);
      for (int i = 7; i >= 0; --i) { Serial.print((val >> i) & 1); }
      Serial.println(")");
    }
  }
}

// ============================================================================
// Data Commands
// ============================================================================

void cmdReadInputs() {
  PCA9555::PortData data;
  PCA9555::Status st = device.readInputs(data);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  Serial.println("=== Input Ports ===");
  printPortBinary("Port 0", data.port0);
  printPortBinary("Port 1", data.port1);
  Serial.printf("  Combined: 0x%04X\n", data.combined());
}

void cmdReadOutputs() {
  PCA9555::PortData data;
  PCA9555::Status st = device.readOutputs(data);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  Serial.println("=== Output Ports ===");
  printPortBinary("Port 0", data.port0);
  printPortBinary("Port 1", data.port1);
  Serial.printf("  Combined: 0x%04X\n", data.combined());
}

void cmdReadConfig() {
  PCA9555::PortData data;
  PCA9555::Status st = device.getConfiguration(data);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  Serial.println("=== Configuration (1=input, 0=output) ===");
  printPortBinary("Port 0", data.port0);
  printPortBinary("Port 1", data.port1);
}

void cmdReadPolarity() {
  PCA9555::PortData data;
  PCA9555::Status st = device.getPolarity(data);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  Serial.println("=== Polarity Inversion (1=inverted) ===");
  printPortBinary("Port 0", data.port0);
  printPortBinary("Port 1", data.port1);
}

void cmdReadOutputPort(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Port port = PCA9555::Port::PORT_0;
  if (!parsePortToken(cursor, port) || hasTrailingArgs(cursor)) {
    LOGE("Usage: read output port <0|1>");
    return;
  }

  uint8_t value = 0;
  PCA9555::Status st = device.readOutput(port, value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.printf("=== Output Port %u Latch ===\n", static_cast<unsigned>(port));
  printPortBinary("Value", value);
}

void cmdReadConfigPort(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Port port = PCA9555::Port::PORT_0;
  if (!parsePortToken(cursor, port) || hasTrailingArgs(cursor)) {
    LOGE("Usage: read config port <0|1>");
    return;
  }

  uint8_t value = 0;
  PCA9555::Status st = device.getPortConfiguration(port, value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.printf("=== Config Port %u (1=input, 0=output) ===\n", static_cast<unsigned>(port));
  printPortBinary("Value", value);
}

void cmdReadPolarityPort(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Port port = PCA9555::Port::PORT_0;
  if (!parsePortToken(cursor, port) || hasTrailingArgs(cursor)) {
    LOGE("Usage: read polarity port <0|1>");
    return;
  }

  uint8_t value = 0;
  PCA9555::Status st = device.getPortPolarity(port, value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.printf("=== Polarity Port %u (1=inverted) ===\n", static_cast<unsigned>(port));
  printPortBinary("Value", value);
}

// ============================================================================
// Pin/Port Write Commands
// ============================================================================

void cmdWritePin(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  bool high = false;
  if (!parsePinToken(cursor, pin) ||
      !parseBinaryToken(cursor, high) ||
      hasTrailingArgs(cursor)) {
    LOGE("Usage: wpin <pin 0-15> <0|1>");
    return;
  }
  PCA9555::Status st = device.writePin(pin, high);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Pin %u set to %d", static_cast<unsigned>(pin), high ? 1 : 0);
}

void cmdReadPin(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  if (!parsePinToken(cursor, pin) || hasTrailingArgs(cursor)) {
    LOGE("Usage: rpin <pin 0-15>");
    return;
  }
  bool state = false;
  PCA9555::Status st = device.readPin(pin, state);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Pin %u input = %d", static_cast<unsigned>(pin), state ? 1 : 0);
}

void cmdReadInputPort(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Port port = PCA9555::Port::PORT_0;
  if (!parsePortToken(cursor, port) || hasTrailingArgs(cursor)) {
    LOGE("Usage: rin <port 0|1>");
    return;
  }

  uint8_t value = 0;
  PCA9555::Status st = device.readInput(port, value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.printf("=== Input Port %u ===\n", static_cast<unsigned>(port));
  printPortBinary("Value", value);
}

void cmdReadOutputPin(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  if (!parsePinToken(cursor, pin) || hasTrailingArgs(cursor)) {
    LOGE("Usage: rout <pin 0-15>");
    return;
  }

  bool high = false;
  PCA9555::Status st = device.readOutputPin(pin, high);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  LOGI("Output latch pin %u = %d", static_cast<unsigned>(pin), high ? 1 : 0);
}

void cmdReadDirectionPin(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  if (!parsePinToken(cursor, pin) || hasTrailingArgs(cursor)) {
    LOGE("Usage: rdir <pin 0-15>");
    return;
  }

  bool input = false;
  PCA9555::Status st = device.getPinDirection(pin, input);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  LOGI("Pin %u direction = %s", static_cast<unsigned>(pin), input ? "INPUT" : "OUTPUT");
}

void cmdReadPolarityPin(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  if (!parsePinToken(cursor, pin) || hasTrailingArgs(cursor)) {
    LOGE("Usage: rpol <pin 0-15>");
    return;
  }

  bool inverted = false;
  PCA9555::Status st = device.getPinPolarity(pin, inverted);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  LOGI("Pin %u polarity = %s", static_cast<unsigned>(pin), inverted ? "INVERTED" : "NORMAL");
}

void cmdPinInfo(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  if (!parsePinToken(cursor, pin) || hasTrailingArgs(cursor)) {
    LOGE("Usage: pininfo <pin 0-15>");
    return;
  }

  bool inputState = false;
  bool outputHigh = false;
  bool directionInput = false;
  bool polarityInverted = false;

  PCA9555::Status st = device.readPin(pin, inputState);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  st = device.readOutputPin(pin, outputHigh);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  st = device.getPinDirection(pin, directionInput);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  st = device.getPinPolarity(pin, polarityInverted);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.printf("=== Pin %u ===\n", static_cast<unsigned>(pin));
  Serial.printf("  Actual level: %d\n", inputState ? 1 : 0);
  Serial.printf("  Output latch: %d\n", outputHigh ? 1 : 0);
  Serial.printf("  Direction: %s\n", directionInput ? "INPUT" : "OUTPUT");
  Serial.printf("  Polarity: %s\n", polarityInverted ? "INVERTED" : "NORMAL");
}

void cmdPins() {
  PCA9555::PortData inputs;
  PCA9555::PortData outputs;
  PCA9555::PortData config;
  PCA9555::PortData polarity;

  PCA9555::Status st = device.readInputs(inputs);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  st = device.readOutputs(outputs);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  st = device.getConfiguration(config);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  st = device.getPolarity(polarity);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.println("=== Pin Summary ===");
  Serial.println("  Pin  In  Out  Dir  Pol");
  for (uint8_t pin = 0; pin < PCA9555::cmd::TOTAL_PINS; ++pin) {
    const bool port1 = pin >= PCA9555::cmd::PINS_PER_PORT;
    const uint8_t shift = static_cast<uint8_t>(pin % PCA9555::cmd::PINS_PER_PORT);
    const uint8_t inputPort = port1 ? inputs.port1 : inputs.port0;
    const uint8_t outputPort = port1 ? outputs.port1 : outputs.port0;
    const uint8_t configPort = port1 ? config.port1 : config.port0;
    const uint8_t polarityPort = port1 ? polarity.port1 : polarity.port0;
    const bool inputLevel = ((inputPort >> shift) & 0x01U) != 0U;
    const bool outputLevel = ((outputPort >> shift) & 0x01U) != 0U;
    const bool inputDir = ((configPort >> shift) & 0x01U) != 0U;
    const bool inverted = ((polarityPort >> shift) & 0x01U) != 0U;

    Serial.printf("  P%02u   %d   %d   %-3s  %s\n",
                  static_cast<unsigned>(pin),
                  inputLevel ? 1 : 0,
                  outputLevel ? 1 : 0,
                  inputDir ? "IN" : "OUT",
                  inverted ? "INV" : "NOR");
  }
}

void cmdTogglePin(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  if (!parsePinToken(cursor, pin) || hasTrailingArgs(cursor)) {
    LOGE("Usage: toggle <pin 0-15>");
    return;
  }

  PCA9555::Status st = device.togglePin(pin);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Pin %u toggled", static_cast<unsigned>(pin));
}

void cmdSetDirection(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  bool input = true;
  if (!parsePinToken(cursor, pin) ||
      !parseDirectionToken(cursor, input) ||
      hasTrailingArgs(cursor)) {
    LOGE("Usage: dir <pin 0-15> <in|out>");
    return;
  }
  PCA9555::Status st = device.setPinDirection(pin, input);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Pin %u set to %s", static_cast<unsigned>(pin), input ? "INPUT" : "OUTPUT");
}

void cmdWritePort(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Port port = PCA9555::Port::PORT_0;
  uint8_t value = 0;
  if (!parsePortToken(cursor, port) ||
      !parseByteToken(cursor, value) ||
      hasTrailingArgs(cursor)) {
    LOGE("Usage: wport <0|1> <0x00-0xFF>");
    return;
  }
  PCA9555::Status st = device.writeOutput(port, value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Port %u output set to 0x%02X", static_cast<unsigned>(port), value);
}

void cmdSetPortDirection(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Port port = PCA9555::Port::PORT_0;
  uint8_t value = 0;
  if (!parsePortToken(cursor, port) ||
      !parseByteToken(cursor, value) ||
      hasTrailingArgs(cursor)) {
    LOGE("Usage: dport <0|1> <0x00-0xFF> (1=input, 0=output)");
    return;
  }
  PCA9555::Status st = device.setPortConfiguration(port, value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Port %u config set to 0x%02X", static_cast<unsigned>(port), value);
}

void cmdSetPortPolarity(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Port port = PCA9555::Port::PORT_0;
  uint8_t value = 0;
  if (!parsePortToken(cursor, port) ||
      !parseByteToken(cursor, value) ||
      hasTrailingArgs(cursor)) {
    LOGE("Usage: wpol <0|1> <0x00-0xFF> (1=inverted)");
    return;
  }
  PCA9555::Status st = device.setPortPolarity(port, value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Port %u polarity set to 0x%02X", static_cast<unsigned>(port), value);
}

void cmdSetPinPolarity(const String& args) {
  const char* cursor = args.c_str();
  PCA9555::Pin pin = 0;
  bool inverted = false;
  if (!parsePinToken(cursor, pin) ||
      !parseBinaryToken(cursor, inverted) ||
      hasTrailingArgs(cursor)) {
    LOGE("Usage: pol <pin 0-15> <0|1>");
    return;
  }

  PCA9555::Status st = device.setPinPolarity(pin, inverted);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  LOGI("Pin %u polarity set to %s",
       static_cast<unsigned>(pin),
       inverted ? "INVERTED" : "NORMAL");
}

// ============================================================================
// Bit Manipulation Commands
// ============================================================================

void cmdSetBits(const String& args) {
  const char* cursor = args.c_str();
  uint16_t mask = 0;
  if (!parseU16Token(cursor, mask) || hasTrailingArgs(cursor)) {
    LOGE("Usage: setbits <0x0000-0xFFFF>");
    return;
  }
  PCA9555::Status st = device.setOutputBits(mask);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Output bits set HIGH: mask=0x%04X", mask);
}

void cmdClearBits(const String& args) {
  const char* cursor = args.c_str();
  uint16_t mask = 0;
  if (!parseU16Token(cursor, mask) || hasTrailingArgs(cursor)) {
    LOGE("Usage: clearbits <0x0000-0xFFFF>");
    return;
  }
  PCA9555::Status st = device.clearOutputBits(mask);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Output bits cleared LOW: mask=0x%04X", mask);
}

void cmdToggleBits(const String& args) {
  const char* cursor = args.c_str();
  uint16_t mask = 0;
  if (!parseU16Token(cursor, mask) || hasTrailingArgs(cursor)) {
    LOGE("Usage: togglebits <0x0000-0xFFFF>");
    return;
  }
  PCA9555::Status st = device.toggleOutputBits(mask);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Output bits toggled: mask=0x%04X", mask);
}

void cmdDirIn(const String& args) {
  const char* cursor = args.c_str();
  uint16_t mask = 0;
  if (!parseU16Token(cursor, mask) || hasTrailingArgs(cursor)) {
    LOGE("Usage: dirin <0x0000-0xFFFF>");
    return;
  }
  PCA9555::Status st = device.configureInputBits(mask);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Pins configured as INPUT: mask=0x%04X", mask);
}

void cmdDirOut(const String& args) {
  const char* cursor = args.c_str();
  uint16_t mask = 0;
  if (!parseU16Token(cursor, mask) || hasTrailingArgs(cursor)) {
    LOGE("Usage: dirout <0x0000-0xFFFF>");
    return;
  }
  PCA9555::Status st = device.configureOutputBits(mask);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Pins configured as OUTPUT: mask=0x%04X", mask);
}

void cmdInvertSet(const String& args) {
  const char* cursor = args.c_str();
  uint16_t mask = 0;
  if (!parseU16Token(cursor, mask) || hasTrailingArgs(cursor)) {
    LOGE("Usage: invertset <0x0000-0xFFFF>");
    return;
  }
  PCA9555::Status st = device.setInvertBits(mask);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Polarity inversion enabled: mask=0x%04X", mask);
}

void cmdInvertClr(const String& args) {
  const char* cursor = args.c_str();
  uint16_t mask = 0;
  if (!parseU16Token(cursor, mask) || hasTrailingArgs(cursor)) {
    LOGE("Usage: invertclr <0x0000-0xFFFF>");
    return;
  }
  PCA9555::Status st = device.clearInvertBits(mask);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Polarity inversion disabled: mask=0x%04X", mask);
}

// ============================================================================
// Raw Register Commands
// ============================================================================

void cmdRegRead(const String& args) {
  const char* cursor = args.c_str();
  long reg = 0;
  if (!parseLongToken(cursor, reg) || reg < 0 || reg > 7 || hasTrailingArgs(cursor)) {
    LOGE("Usage: rreg <0-7>");
    return;
  }
  uint8_t value = 0;
  PCA9555::Status st = device.readRegister(static_cast<uint8_t>(reg), value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  Serial.printf("  Reg 0x%02X = 0x%02X (", static_cast<int>(reg), value);
  for (int i = 7; i >= 0; --i) { Serial.print((value >> i) & 1); }
  Serial.println(")");
}

void cmdRegWrite(const String& args) {
  const char* cursor = args.c_str();
  long reg = 0;
  uint8_t value = 0;
  if (!parseLongToken(cursor, reg) ||
      reg < 2 || reg > 7 ||
      !parseByteToken(cursor, value) ||
      hasTrailingArgs(cursor)) {
    LOGE("Usage: wreg <2-7> <0x00-0xFF>");
    return;
  }
  PCA9555::Status st = device.writeRegister(static_cast<uint8_t>(reg), value);
  if (!st.ok()) {
    printStatus(st);
    return;
  }
  LOGI("Reg 0x%02X set to 0x%02X", static_cast<int>(reg), value);
}

void cmdRegsRead(const String& args) {
  const char* cursor = args.c_str();
  long reg = 0;
  long len = 0;
  if (!parseLongToken(cursor, reg) ||
      !parseLongToken(cursor, len) ||
      reg < 0 || reg > 7 ||
      len < 1 || len > 2 ||
      hasTrailingArgs(cursor)) {
    LOGE("Usage: rregs <0-7> <1-2>");
    return;
  }

  uint8_t values[2] = {};
  PCA9555::Status st = device.readRegisters(static_cast<uint8_t>(reg),
                                            values,
                                            static_cast<size_t>(len));
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.printf("  Regs 0x%02X..0x%02X =",
                static_cast<unsigned>(reg),
                static_cast<unsigned>(reg + len - 1));
  for (long i = 0; i < len; ++i) {
    Serial.printf(" 0x%02X", values[static_cast<size_t>(i)]);
  }
  Serial.print(" (");
  for (long i = 0; i < len; ++i) {
    for (int bit = 7; bit >= 0; --bit) {
      Serial.print((values[static_cast<size_t>(i)] >> bit) & 1);
    }
    if (i + 1 < len) {
      Serial.print(' ');
    }
  }
  Serial.println(")");
}

void cmdRegsWrite(const String& args) {
  const char* cursor = args.c_str();
  long reg = 0;
  uint8_t values[2] = {};
  if (!parseLongToken(cursor, reg) ||
      reg < 2 || reg > 7 ||
      !parseByteToken(cursor, values[0])) {
    LOGE("Usage: wregs <2-7> <0x00-0xFF> [0x00-0xFF]");
    return;
  }

  size_t len = 1;
  const char* afterFirst = skipWhitespace(cursor);
  if (*afterFirst != '\0') {
    cursor = afterFirst;
    if (!parseByteToken(cursor, values[1]) || hasTrailingArgs(cursor)) {
      LOGE("Usage: wregs <2-7> <0x00-0xFF> [0x00-0xFF]");
      return;
    }
    len = 2;
  }

  PCA9555::Status st = device.writeRegisters(static_cast<uint8_t>(reg), values, len);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  if (len == 1) {
    LOGI("Reg 0x%02X set to 0x%02X", static_cast<int>(reg), values[0]);
  } else {
    LOGI("Regs 0x%02X..0x%02X set to 0x%02X 0x%02X",
         static_cast<int>(reg),
         static_cast<int>(reg + 1),
         values[0],
         values[1]);
  }
}

// ============================================================================
// Self-Test
// ============================================================================

void runSelfTest() {
  struct TestStats {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t skip = 0;
  } stats;

  enum class SelftestOutcome : uint8_t { PASS, FAIL, SKIP };
  auto report = [&](const char* name, SelftestOutcome outcome, const char* note) {
    const bool passed = (outcome == SelftestOutcome::PASS);
    const bool skipped = (outcome == SelftestOutcome::SKIP);
    const char* color = skipped ? LOG_COLOR_YELLOW : LOG_COLOR_RESULT(passed);
    const char* tag = skipped ? "SKIP" : (passed ? "PASS" : "FAIL");
    Serial.printf("  [%s%s%s] %s", color, tag, LOG_COLOR_RESET, name);
    if (note && note[0]) {
      Serial.printf(" - %s", note);
    }
    Serial.println();
    if (skipped) {
      stats.skip++;
    } else if (passed) {
      stats.pass++;
    } else {
      stats.fail++;
    }
  };
  auto reportCheck = [&](const char* name, bool passed, const char* note) {
    report(name, passed ? SelftestOutcome::PASS : SelftestOutcome::FAIL, note);
  };
  auto reportSkip = [&](const char* name, const char* note) {
    report(name, SelftestOutcome::SKIP, note);
  };

  Serial.println("=== PCA9555 selftest (safe commands) ===");

  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint8_t consBefore = device.consecutiveFailures();

  // --- probe ---
  PCA9555::Status st = device.probe();
  if (st.code == PCA9555::Err::NOT_INITIALIZED) {
    reportSkip("probe responds", "driver not initialized");
    reportSkip("remaining checks", "selftest aborted");
    Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                  goodIfNonZeroColor(stats.pass), static_cast<unsigned long>(stats.pass), LOG_COLOR_RESET,
                  goodIfZeroColor(stats.fail), static_cast<unsigned long>(stats.fail), LOG_COLOR_RESET,
                  skipCountColor(stats.skip), static_cast<unsigned long>(stats.skip), LOG_COLOR_RESET);
    return;
  }
  const bool probeHealthUnchanged =
      device.totalSuccess() == succBefore &&
      device.totalFailures() == failBefore &&
      device.consecutiveFailures() == consBefore;
  reportCheck("probe responds", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("probe no-health-side-effects", probeHealthUnchanged, "");

  // --- readInputs ---
  PCA9555::PortData data;
  st = device.readInputs(data);
  reportCheck("readInputs", st.ok(), st.ok() ? "" : errToStr(st.code));
  uint8_t inputPort = 0;
  st = device.readInput(PCA9555::Port::PORT_0, inputPort);
  reportCheck("readInput(P0)", st.ok(), st.ok() ? "" : errToStr(st.code));

  // --- readOutputs ---
  st = device.readOutputs(data);
  reportCheck("readOutputs", st.ok(), st.ok() ? "" : errToStr(st.code));
  uint8_t outputPort = 0;
  st = device.readOutput(PCA9555::Port::PORT_0, outputPort);
  reportCheck("readOutput(P0)", st.ok(), st.ok() ? "" : errToStr(st.code));
  bool outputPin = false;
  st = device.readOutputPin(0, outputPin);
  reportCheck("readOutputPin(0)", st.ok(), st.ok() ? "" : errToStr(st.code));

  // --- getConfiguration ---
  PCA9555::PortData configData;
  st = device.getConfiguration(configData);
  reportCheck("getConfiguration", st.ok(), st.ok() ? "" : errToStr(st.code));
  uint8_t configPort = 0;
  st = device.getPortConfiguration(PCA9555::Port::PORT_0, configPort);
  reportCheck("getPortConfiguration(P0)", st.ok(), st.ok() ? "" : errToStr(st.code));
  bool pinDirection = false;
  st = device.getPinDirection(0, pinDirection);
  reportCheck("getPinDirection(0)", st.ok(), st.ok() ? "" : errToStr(st.code));

  // --- getPolarity ---
  PCA9555::PortData polData;
  st = device.getPolarity(polData);
  reportCheck("getPolarity", st.ok(), st.ok() ? "" : errToStr(st.code));
  uint8_t polarityPort = 0;
  st = device.getPortPolarity(PCA9555::Port::PORT_0, polarityPort);
  reportCheck("getPortPolarity(P0)", st.ok(), st.ok() ? "" : errToStr(st.code));
  bool pinPolarity = false;
  st = device.getPinPolarity(0, pinPolarity);
  reportCheck("getPinPolarity(0)", st.ok(), st.ok() ? "" : errToStr(st.code));

  // --- writeOutput + readback ---
  PCA9555::PortData savedOut;
  device.readOutputs(savedOut);
  st = device.writeOutput(PCA9555::Port::PORT_0, 0xAA);
  if (st.ok()) {
    PCA9555::PortData readback;
    device.readOutputs(readback);
    reportCheck("writeOutput(P0, 0xAA) + readback", readback.port0 == 0xAA, "");
  } else {
    reportCheck("writeOutput(P0, 0xAA)", false, errToStr(st.code));
  }
  st = device.writeOutput(PCA9555::Port::PORT_1, 0x55);
  if (st.ok()) {
    PCA9555::PortData readback;
    device.readOutputs(readback);
    reportCheck("writeOutput(P1, 0x55) + readback", readback.port1 == 0x55, "");
  } else {
    reportCheck("writeOutput(P1, 0x55)", false, errToStr(st.code));
  }
  // Restore outputs
  device.writeOutput(PCA9555::Port::PORT_0, savedOut.port0);
  device.writeOutput(PCA9555::Port::PORT_1, savedOut.port1);

  // --- setPortConfiguration + readback ---
  PCA9555::PortData savedCfg;
  device.getConfiguration(savedCfg);
  st = device.setPortConfiguration(PCA9555::Port::PORT_0, 0x0F);
  if (st.ok()) {
    PCA9555::PortData readback;
    device.getConfiguration(readback);
    reportCheck("setPortConfiguration(P0, 0x0F) + readback", readback.port0 == 0x0F, "");
  } else {
    reportCheck("setPortConfiguration(P0, 0x0F)", false, errToStr(st.code));
  }
  // Restore config
  device.setPortConfiguration(PCA9555::Port::PORT_0, savedCfg.port0);
  device.setPortConfiguration(PCA9555::Port::PORT_1, savedCfg.port1);

  // --- setPortPolarity + readback ---
  PCA9555::PortData savedPol;
  device.getPolarity(savedPol);
  st = device.setPortPolarity(PCA9555::Port::PORT_0, 0x0F);
  if (st.ok()) {
    PCA9555::PortData readback;
    device.getPolarity(readback);
    reportCheck("setPortPolarity(P0, 0x0F) + readback", readback.port0 == 0x0F, "");
  } else {
    reportCheck("setPortPolarity(P0, 0x0F)", false, errToStr(st.code));
  }
  st = device.setPinPolarity(8, true);
  if (st.ok()) {
    PCA9555::PortData readback;
    device.getPolarity(readback);
    reportCheck("setPinPolarity(8, 1) + readback", (readback.port1 & 0x01U) != 0U, "");
  } else {
    reportCheck("setPinPolarity(8, 1)", false, errToStr(st.code));
  }
  // Restore polarity
  device.setPortPolarity(PCA9555::Port::PORT_0, savedPol.port0);
  device.setPortPolarity(PCA9555::Port::PORT_1, savedPol.port1);

  // --- readRegister all 8 ---
  bool allRegsOk = true;
  for (uint8_t r = 0; r < 8; ++r) {
    uint8_t val = 0;
    if (!device.readRegister(r, val).ok()) {
      allRegsOk = false;
    }
  }
  reportCheck("readRegister(0-7) all OK", allRegsOk, "");

  // --- bulk register helpers ---
  PCA9555::PortData savedBulkOut;
  device.readOutputs(savedBulkOut);
  const uint8_t bulkOut[2] = {0xA5, 0x5A};
  st = device.writeRegisters(PCA9555::cmd::REG_OUTPUT_PORT_0, bulkOut, 2);
  if (st.ok()) {
    uint8_t bulkRead[2] = {};
    st = device.readRegisters(PCA9555::cmd::REG_OUTPUT_PORT_0, bulkRead, 2);
    reportCheck("writeRegisters(OUT, 2) + readback",
           st.ok() && bulkRead[0] == bulkOut[0] && bulkRead[1] == bulkOut[1], "");
  } else {
    reportCheck("writeRegisters(OUT, 2)", false, errToStr(st.code));
  }
  const uint8_t restoreBulkOut[2] = {savedBulkOut.port0, savedBulkOut.port1};
  device.writeRegisters(PCA9555::cmd::REG_OUTPUT_PORT_0, restoreBulkOut, 2);

  uint8_t bulkCfg[2] = {};
  st = device.readRegisters(PCA9555::cmd::REG_CONFIG_PORT_0, bulkCfg, 2);
  reportCheck("readRegisters(CFG, 2)", st.ok(), st.ok() ? "" : errToStr(st.code));

  // --- writeRegister (writable range 2-7) ---
  uint8_t savedReg2 = 0;
  device.readRegister(2, savedReg2);
  st = device.writeRegister(2, 0xBB);
  if (st.ok()) {
    uint8_t readback = 0;
    device.readRegister(2, readback);
    reportCheck("writeRegister(2, 0xBB) + readback", readback == 0xBB, "");
  } else {
    reportCheck("writeRegister(2, 0xBB)", false, errToStr(st.code));
  }
  device.writeRegister(2, savedReg2); // restore

  // --- recover ---
  st = device.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : errToStr(st.code));

  // --- isOnline ---
  reportCheck("isOnline", device.isOnline(), "");

  // --- health delta ---
  const uint32_t succDelta = device.totalSuccess() - succBefore;
  const uint32_t failDelta = device.totalFailures() - failBefore;

  Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                goodIfNonZeroColor(stats.pass), static_cast<unsigned long>(stats.pass), LOG_COLOR_RESET,
                goodIfZeroColor(stats.fail), static_cast<unsigned long>(stats.fail), LOG_COLOR_RESET,
                skipCountColor(stats.skip), static_cast<unsigned long>(stats.skip), LOG_COLOR_RESET);
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(succDelta),
                static_cast<unsigned long>(succDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
}

// ============================================================================
// Stress Tests
// ============================================================================

void resetStressStats(int count) {
  stressStats.active = true;
  stressStats.total = count;
  stressStats.okCount = 0;
  stressStats.failCount = 0;
  stressStats.startMs = millis();
  stressStats.hasFailure = false;
  stressStats.firstFailure = PCA9555::Status::Ok();
  stressStats.lastFailure = PCA9555::Status::Ok();
  stressStats.succBefore = device.totalSuccess();
  stressStats.failBefore = device.totalFailures();
}

void finishStressStats() {
  stressStats.active = false;
  const uint32_t elapsed = millis() - stressStats.startMs;
  const int total = stressStats.okCount + stressStats.failCount;
  const float successPct = (total > 0)
                               ? (100.0f * static_cast<float>(stressStats.okCount) /
                                  static_cast<float>(total))
                               : 0.0f;

  Serial.println("=== Stress Results ===");
  Serial.printf("  Total: %sok=%lu%s %sfail=%lu%s (%s%.2f%%%s)\n",
                goodIfNonZeroColor(static_cast<uint32_t>(stressStats.okCount)),
                static_cast<unsigned long>(stressStats.okCount),
                LOG_COLOR_RESET,
                goodIfZeroColor(static_cast<uint32_t>(stressStats.failCount)),
                static_cast<unsigned long>(stressStats.failCount),
                LOG_COLOR_RESET,
                successRateColor(successPct),
                successPct,
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(elapsed));
  if (elapsed > 0) {
    Serial.printf("  Rate: %.2f ops/s\n",
                  (1000.0f * static_cast<float>(total)) / static_cast<float>(elapsed));
  }
  const uint32_t succDelta = device.totalSuccess() - stressStats.succBefore;
  const uint32_t failDelta = device.totalFailures() - stressStats.failBefore;
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(succDelta),
                static_cast<unsigned long>(succDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
  if (stressStats.hasFailure) {
    Serial.println("  First failure:");
    printStatus(stressStats.firstFailure);
    if (stressStats.failCount > 1) {
      Serial.println("  Last failure:");
      printStatus(stressStats.lastFailure);
    }
  }
}

void runStressMix(int count) {
  const uint32_t startMs = millis();
  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();

  struct OpStats {
    const char* name;
    uint32_t ok;
    uint32_t fail;
  };
  static constexpr int OP_COUNT = 6;
  OpStats ops[OP_COUNT] = {
    {"readInputs",    0, 0},
    {"readOutputs",   0, 0},
    {"getConfig",     0, 0},
    {"getPolarity",   0, 0},
    {"readRegister",  0, 0},
    {"writeOutput",   0, 0},
  };

  // Save output state for restore
  PCA9555::PortData savedOut;
  device.readOutputs(savedOut);

  for (int i = 0; i < count; ++i) {
    const int op = i % OP_COUNT;
    PCA9555::Status st;
    PCA9555::PortData d;
    uint8_t val = 0;

    switch (op) {
      case 0: st = device.readInputs(d);            break;
      case 1: st = device.readOutputs(d);            break;
      case 2: st = device.getConfiguration(d);       break;
      case 3: st = device.getPolarity(d);            break;
      case 4: st = device.readRegister(0, val);      break;
      case 5: st = device.writeOutput(PCA9555::Port::PORT_0, savedOut.port0); break;
      default: break;
    }

    if (st.ok()) {
      ops[op].ok++;
    } else {
      ops[op].fail++;
      LOGV(verboseMode, "  cycle %d op=%s: %s", i, ops[op].name, errToStr(st.code));
    }
  }

  // Restore outputs
  device.writeOutput(PCA9555::Port::PORT_0, savedOut.port0);
  device.writeOutput(PCA9555::Port::PORT_1, savedOut.port1);

  const uint32_t elapsed = millis() - startMs;
  uint32_t totalOk = 0, totalFail = 0;
  for (int i = 0; i < OP_COUNT; ++i) {
    totalOk += ops[i].ok;
    totalFail += ops[i].fail;
  }
  const float successPct = (count > 0)
                               ? (100.0f * static_cast<float>(totalOk) /
                                  static_cast<float>(count))
                               : 0.0f;

  Serial.println("=== stress_mix summary ===");
  Serial.printf("  Total: %sok=%lu%s %sfail=%lu%s (%s%.2f%%%s)\n",
                goodIfNonZeroColor(totalOk),
                static_cast<unsigned long>(totalOk),
                LOG_COLOR_RESET,
                goodIfZeroColor(totalFail),
                static_cast<unsigned long>(totalFail),
                LOG_COLOR_RESET,
                successRateColor(successPct),
                successPct,
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(elapsed));
  if (elapsed > 0) {
    Serial.printf("  Rate: %.2f ops/s\n",
                  (1000.0f * static_cast<float>(count)) / static_cast<float>(elapsed));
  }
  for (int i = 0; i < OP_COUNT; ++i) {
    Serial.printf("  %-12s %sok=%lu%s %sfail=%lu%s\n",
                  ops[i].name,
                  goodIfNonZeroColor(ops[i].ok),
                  static_cast<unsigned long>(ops[i].ok),
                  LOG_COLOR_RESET,
                  goodIfZeroColor(ops[i].fail),
                  static_cast<unsigned long>(ops[i].fail),
                  LOG_COLOR_RESET);
  }

  const uint32_t succDelta = device.totalSuccess() - succBefore;
  const uint32_t failDelta = device.totalFailures() - failBefore;
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(succDelta),
                static_cast<unsigned long>(succDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
}

// ============================================================================
// Pin Test Commands
// ============================================================================

void cmdSweep(const String& args) {
  const char* cursor = args.c_str();
  long delayMs = 200;
  if (*skipWhitespace(cursor) != '\0') {
    if (!parseLongToken(cursor, delayMs) || hasTrailingArgs(cursor) ||
        delayMs < 0 || delayMs > 10000) {
      LOGE("Usage: sweep [delay_ms] (0-10000, default 200)");
      return;
    }
  }

  // Save current config and outputs
  PCA9555::PortData savedCfg;
  PCA9555::PortData savedOut;
  PCA9555::Status st = device.getConfiguration(savedCfg);
  if (!st.ok()) { printStatus(st); return; }
  st = device.readOutputs(savedOut);
  if (!st.ok()) { printStatus(st); return; }

  // Set all pins to output, all low
  st = device.writeOutputs({0x00, 0x00});
  if (!st.ok()) { printStatus(st); return; }
  st = device.setConfiguration({0x00, 0x00});
  if (!st.ok()) { printStatus(st); return; }

  Serial.printf("=== Sweep Test (delay=%ld ms) ===\n", delayMs);
  int pass = 0, fail = 0;

  // Phase 1: turn pins ON one-by-one (accumulating)
  Serial.println("  -- ON sweep --");
  for (uint8_t pin = 0; pin < PCA9555::cmd::TOTAL_PINS; ++pin) {
    st = device.writePin(pin, true);
    if (!st.ok()) {
      Serial.printf("  P%02u: %s[FAIL]%s set high - %s\n",
                    pin, LOG_COLOR_RED, LOG_COLOR_RESET, errToStr(st.code));
      fail++;
      continue;
    }

    if (delayMs > 0) delay(static_cast<unsigned long>(delayMs));

    // Verify: all pins 0..pin should be HIGH
    PCA9555::PortData readback;
    st = device.readOutputs(readback);
    const uint16_t expected = static_cast<uint16_t>((1U << (pin + 1)) - 1U);
    if (!st.ok() || readback.combined() != expected) {
      Serial.printf("  P%02u: %s[FAIL]%s ON readback 0x%04X != 0x%04X\n",
                    pin, LOG_COLOR_RED, LOG_COLOR_RESET,
                    readback.combined(), expected);
      fail++;
    } else {
      Serial.printf("  P%02u: %s[OK]%s ON (0x%04X)\n",
                    pin, LOG_COLOR_GREEN, LOG_COLOR_RESET, expected);
      pass++;
    }
  }

  // Phase 2: turn pins OFF one-by-one (draining)
  Serial.println("  -- OFF sweep --");
  for (uint8_t pin = 0; pin < PCA9555::cmd::TOTAL_PINS; ++pin) {
    st = device.writePin(pin, false);
    if (!st.ok()) {
      Serial.printf("  P%02u: %s[FAIL]%s set low - %s\n",
                    pin, LOG_COLOR_RED, LOG_COLOR_RESET, errToStr(st.code));
      fail++;
      continue;
    }

    if (delayMs > 0) delay(static_cast<unsigned long>(delayMs));

    // Verify: pins 0..pin should be LOW, pins (pin+1)..15 should still be HIGH
    PCA9555::PortData readback;
    st = device.readOutputs(readback);
    const uint16_t expected = (pin < 15)
        ? static_cast<uint16_t>(0xFFFFU << (pin + 1))
        : static_cast<uint16_t>(0x0000U);
    if (!st.ok() || readback.combined() != expected) {
      Serial.printf("  P%02u: %s[FAIL]%s OFF readback 0x%04X != 0x%04X\n",
                    pin, LOG_COLOR_RED, LOG_COLOR_RESET,
                    readback.combined(), expected);
      fail++;
    } else {
      Serial.printf("  P%02u: %s[OK]%s OFF (0x%04X)\n",
                    pin, LOG_COLOR_GREEN, LOG_COLOR_RESET, expected);
      pass++;
    }
  }

  // Restore
  device.setConfiguration(savedCfg);
  device.writeOutputs(savedOut);

  Serial.println("--- Summary ---");
  Serial.printf("  %s%d passed%s, %s%d failed%s\n",
                LOG_COLOR_GREEN, pass, LOG_COLOR_RESET,
                (fail > 0) ? LOG_COLOR_RED : LOG_COLOR_GREEN, fail, LOG_COLOR_RESET);
}

void cmdWalk(const String& args) {
  const char* cursor = args.c_str();
  long delayMs = 200;
  if (*skipWhitespace(cursor) != '\0') {
    if (!parseLongToken(cursor, delayMs) || hasTrailingArgs(cursor) ||
        delayMs < 0 || delayMs > 10000) {
      LOGE("Usage: walk [delay_ms] (0-10000, default 200)");
      return;
    }
  }

  // Save current config and outputs
  PCA9555::PortData savedCfg;
  PCA9555::PortData savedOut;
  PCA9555::Status st = device.getConfiguration(savedCfg);
  if (!st.ok()) { printStatus(st); return; }
  st = device.readOutputs(savedOut);
  if (!st.ok()) { printStatus(st); return; }

  // Set all pins to output
  st = device.setConfiguration({0x00, 0x00});
  if (!st.ok()) { printStatus(st); return; }

  Serial.printf("=== Walking-1 Test (delay=%ld ms) ===\n", delayMs);
  int pass = 0, fail = 0;

  for (uint8_t pin = 0; pin < PCA9555::cmd::TOTAL_PINS; ++pin) {
    // Only this pin high, all others low
    const uint16_t pattern = static_cast<uint16_t>(1U << pin);
    const PCA9555::PortData out = PCA9555::PortData::fromCombined(pattern);

    st = device.writeOutputs(out);
    if (!st.ok()) { fail++; continue; }

    if (delayMs > 0) delay(static_cast<unsigned long>(delayMs));

    // Readback verify
    PCA9555::PortData readback;
    st = device.readOutputs(readback);
    if (!st.ok() || readback.combined() != pattern) {
      Serial.printf("  P%02u: %s[FAIL]%s readback 0x%04X != 0x%04X\n",
                    pin, LOG_COLOR_RED, LOG_COLOR_RESET,
                    readback.combined(), pattern);
      fail++;
    } else {
      Serial.printf("  P%02u: %s[OK]%s P0=0x%02X P1=0x%02X\n",
                    pin, LOG_COLOR_GREEN, LOG_COLOR_RESET, out.port0, out.port1);
      pass++;
    }
  }

  // All off, then restore
  device.writeOutputs({0x00, 0x00});
  device.setConfiguration(savedCfg);
  device.writeOutputs(savedOut);

  Serial.println("--- Summary ---");
  Serial.printf("  %s%d passed%s, %s%d failed%s\n",
                LOG_COLOR_GREEN, pass, LOG_COLOR_RESET,
                (fail > 0) ? LOG_COLOR_RED : LOG_COLOR_GREEN, fail, LOG_COLOR_RESET);
}

void cmdAllHigh() {
  // Set output values before direction to avoid glitches
  PCA9555::Status st = device.writeOutputs({0xFF, 0xFF});
  if (!st.ok()) { printStatus(st); return; }
  st = device.setConfiguration({0x00, 0x00});
  if (!st.ok()) { printStatus(st); return; }
  LOGI("All 16 pins set to OUTPUT HIGH");
}

void cmdAllLow() {
  // Set output values before direction to avoid glitches
  PCA9555::Status st = device.writeOutputs({0x00, 0x00});
  if (!st.ok()) { printStatus(st); return; }
  st = device.setConfiguration({0x00, 0x00});
  if (!st.ok()) { printStatus(st); return; }
  LOGI("All 16 pins set to OUTPUT LOW");
}

// ============================================================================
// Help
// ============================================================================

void printHelp() {
  auto helpSection = [](const char* title) {
    Serial.printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
  };
  auto helpItem = [](const char* cmd, const char* desc) {
    Serial.printf("  %s%-32s%s - %s\n", LOG_COLOR_CYAN, cmd, LOG_COLOR_RESET, desc);
  };

  Serial.println();
  Serial.printf("%s=== PCA9555 CLI Help ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);

  helpSection("Common");
  helpItem("help / ?", "Show this help");
  helpItem("version / ver", "Print firmware and library version info");
  helpItem("scan", "Scan I2C bus");

  helpSection("Read");
  helpItem("read / inputs", "Read both input ports");
  helpItem("read input port <P> / rin <P>", "Read one input port");
  helpItem("read outputs / outputs", "Read output registers");
  helpItem("read output port <P>", "Read one output-port latch register");
  helpItem("read config / config", "Read configuration (direction) registers");
  helpItem("read config port <P>", "Read one configuration register");
  helpItem("read polarity / polarity", "Read polarity inversion registers");
  helpItem("read polarity port <P>", "Read one polarity register");
  helpItem("read pin <N> / rpin <N>", "Read input pin N (0-15)");
  helpItem("read outpin <N> / rout <N>", "Read output latch bit for pin N");
  helpItem("read dirpin <N> / rdir <N>", "Read pin direction for pin N");
  helpItem("read polpin <N> / rpol <N>", "Read pin polarity inversion for pin N");
  helpItem("pininfo <N>", "Show actual level, latch, direction, and polarity");
  helpItem("pins", "Show a 16-pin summary table");
  helpItem("cfg / settings", "Print active driver settings snapshot");
  helpItem("dump", "Dump all 8 registers");

  helpSection("Write");
  helpItem("write pin <N> <0|1> / wpin <N> <0|1>", "Set output pin N to 0 or 1");
  helpItem("toggle <N>", "Toggle output pin N");
  helpItem("dir pin <N> <in|out> / dir <N> <in|out>", "Set pin N direction");
  helpItem("write port <P> <V> / wport <P> <V>", "Write port P (0/1) output to V");
  helpItem("dir port <P> <V> / dport <P> <V>", "Set port direction (1=in, 0=out)");
  helpItem("polarity pin <N> <0|1> / pol <N> <0|1>", "Set single-pin polarity inversion");
  helpItem("polarity port <P> <V> / wpol <P> <V>", "Set port polarity inversion");

  helpSection("Bit Manipulation (16-bit mask, single I2C burst)");
  helpItem("setbits <M> / sb <M>", "Set output bits HIGH (OR mask)");
  helpItem("clearbits <M> / cb <M>", "Clear output bits LOW (AND ~mask)");
  helpItem("togglebits <M> / tb <M>", "Toggle output bits (XOR mask)");
  helpItem("dirin <M>", "Configure masked pins as INPUT");
  helpItem("dirout <M>", "Configure masked pins as OUTPUT");
  helpItem("invertset <M>", "Enable polarity inversion for masked pins");
  helpItem("invertclr <M>", "Disable polarity inversion for masked pins");

  helpSection("Raw Register");
  helpItem("read reg <R> / rreg <R>", "Read register R (0-7)");
  helpItem("read regs <R> <N> / rregs <R> <N>", "Read 1-2 registers in one pair");
  helpItem("write reg <R> <V> / wreg <R> <V>", "Write register R (2-7) to V");
  helpItem("write regs <R> <V0> [V1] / wregs <R> <V0> [V1]",
           "Write 1-2 registers in one pair");

  helpSection("Testing");
  helpItem("sweep [delay_ms]", "Fill ON then drain OFF, pin by pin (accumulating)");
  helpItem("walk [delay_ms]", "Walking-1: single pin HIGH moves across all 16");
  helpItem("allhigh", "Set all 16 pins to output HIGH");
  helpItem("alllow", "Set all 16 pins to output LOW");

  helpSection("Diagnostics");
  helpItem("drv", "Show driver state and health");
  helpItem("probe", "Probe device (no health tracking)");
  helpItem("recover", "Manual recovery attempt");
  helpItem("verbose [0|1]", "Enable/disable verbose output");
  helpItem("selftest", "Run safe command self-test report");
  helpItem("stress [N]", "Run N readInputs cycles (default 10)");
  helpItem("stress_mix [N]", "Run N mixed-operation cycles (default 50)");
}

// ============================================================================
// Command dispatch
// ============================================================================

void processCommand(const String& cmdLine) {
  String cmd = cmdLine;
  cmd.trim();
  if (cmd.length() == 0) return;

  // ---- Common ----
  if (cmd == "help" || cmd == "?") {
    printHelp();
    return;
  }

  if (cmd == "version" || cmd == "ver") {
    printVersionInfo();
    return;
  }

  if (cmd == "scan") {
    bus_diag::scan();
    return;
  }

  // ---- Read ----
  if (cmd == "read" || cmd == "inputs" || cmd == "read inputs") {
    cmdReadInputs();
    return;
  }

  if (cmd.startsWith("read input port ")) {
    cmdReadInputPort(cmd.substring(16));
    return;
  }

  if (cmd.startsWith("rin ")) {
    cmdReadInputPort(cmd.substring(4));
    return;
  }

  if (cmd == "outputs" || cmd == "read outputs") {
    cmdReadOutputs();
    return;
  }

  if (cmd.startsWith("read output port ")) {
    cmdReadOutputPort(cmd.substring(17));
    return;
  }

  if (cmd == "config" || cmd == "read config") {
    cmdReadConfig();
    return;
  }

  if (cmd.startsWith("read config port ")) {
    cmdReadConfigPort(cmd.substring(17));
    return;
  }

  if (cmd == "polarity" || cmd == "read polarity") {
    cmdReadPolarity();
    return;
  }

  if (cmd.startsWith("read polarity port ")) {
    cmdReadPolarityPort(cmd.substring(19));
    return;
  }

  if (cmd == "cfg" || cmd == "settings") {
    printSettings();
    return;
  }

  if (cmd.startsWith("read pin ")) {
    cmdReadPin(cmd.substring(9));
    return;
  }

  if (cmd.startsWith("rpin ")) {
    cmdReadPin(cmd.substring(5));
    return;
  }

  if (cmd.startsWith("read outpin ")) {
    cmdReadOutputPin(cmd.substring(12));
    return;
  }

  if (cmd.startsWith("rout ")) {
    cmdReadOutputPin(cmd.substring(5));
    return;
  }

  if (cmd.startsWith("read dirpin ")) {
    cmdReadDirectionPin(cmd.substring(12));
    return;
  }

  if (cmd.startsWith("rdir ")) {
    cmdReadDirectionPin(cmd.substring(5));
    return;
  }

  if (cmd.startsWith("read polpin ")) {
    cmdReadPolarityPin(cmd.substring(12));
    return;
  }

  if (cmd.startsWith("rpol ")) {
    cmdReadPolarityPin(cmd.substring(5));
    return;
  }

  if (cmd.startsWith("pininfo ")) {
    cmdPinInfo(cmd.substring(8));
    return;
  }

  if (cmd == "pins") {
    cmdPins();
    return;
  }

  if (cmd.startsWith("read regs ")) {
    cmdRegsRead(cmd.substring(10));
    return;
  }

  if (cmd.startsWith("rregs ")) {
    cmdRegsRead(cmd.substring(6));
    return;
  }

  if (cmd.startsWith("read reg ")) {
    cmdRegRead(cmd.substring(9));
    return;
  }

  if (cmd == "dump") {
    cmdDump();
    return;
  }

  // ---- Write ----
  if (cmd.startsWith("write pin ")) {
    cmdWritePin(cmd.substring(10));
    return;
  }

  if (cmd.startsWith("wpin ")) {
    cmdWritePin(cmd.substring(5));
    return;
  }

  if (cmd.startsWith("toggle ")) {
    cmdTogglePin(cmd.substring(7));
    return;
  }

  if (cmd.startsWith("dir pin ")) {
    cmdSetDirection(cmd.substring(8));
    return;
  }

  if (cmd.startsWith("dir ")) {
    cmdSetDirection(cmd.substring(4));
    return;
  }

  if (cmd.startsWith("write port ")) {
    cmdWritePort(cmd.substring(11));
    return;
  }

  if (cmd.startsWith("wport ")) {
    cmdWritePort(cmd.substring(6));
    return;
  }

  if (cmd.startsWith("dir port ")) {
    cmdSetPortDirection(cmd.substring(9));
    return;
  }

  if (cmd.startsWith("dport ")) {
    cmdSetPortDirection(cmd.substring(6));
    return;
  }

  if (cmd.startsWith("polarity pin ")) {
    cmdSetPinPolarity(cmd.substring(13));
    return;
  }

  if (cmd.startsWith("pol ")) {
    cmdSetPinPolarity(cmd.substring(4));
    return;
  }

  if (cmd.startsWith("polarity port ")) {
    cmdSetPortPolarity(cmd.substring(14));
    return;
  }

  if (cmd.startsWith("wpol ")) {
    cmdSetPortPolarity(cmd.substring(5));
    return;
  }

  // ---- Bit Manipulation ----
  if (cmd.startsWith("setbits ")) {
    cmdSetBits(cmd.substring(8));
    return;
  }

  if (cmd.startsWith("sb ")) {
    cmdSetBits(cmd.substring(3));
    return;
  }

  if (cmd.startsWith("clearbits ")) {
    cmdClearBits(cmd.substring(10));
    return;
  }

  if (cmd.startsWith("cb ")) {
    cmdClearBits(cmd.substring(3));
    return;
  }

  if (cmd.startsWith("togglebits ")) {
    cmdToggleBits(cmd.substring(11));
    return;
  }

  if (cmd.startsWith("tb ")) {
    cmdToggleBits(cmd.substring(3));
    return;
  }

  if (cmd.startsWith("dirin ")) {
    cmdDirIn(cmd.substring(6));
    return;
  }

  if (cmd.startsWith("dirout ")) {
    cmdDirOut(cmd.substring(7));
    return;
  }

  if (cmd.startsWith("invertset ")) {
    cmdInvertSet(cmd.substring(10));
    return;
  }

  if (cmd.startsWith("invertclr ")) {
    cmdInvertClr(cmd.substring(10));
    return;
  }

  // ---- Raw Register ----
  if (cmd.startsWith("write regs ")) {
    cmdRegsWrite(cmd.substring(11));
    return;
  }

  if (cmd.startsWith("wregs ")) {
    cmdRegsWrite(cmd.substring(6));
    return;
  }

  if (cmd.startsWith("write reg ")) {
    cmdRegWrite(cmd.substring(10));
    return;
  }

  if (cmd.startsWith("rreg ")) {
    cmdRegRead(cmd.substring(5));
    return;
  }

  if (cmd.startsWith("wreg ")) {
    cmdRegWrite(cmd.substring(5));
    return;
  }

  // ---- Diagnostics ----
  if (cmd == "drv") {
    printDriverHealth();
    return;
  }

  if (cmd == "probe") {
    LOGI("Probing device (no health tracking)...");
    printStatus(device.probe());
    return;
  }

  if (cmd == "recover") {
    LOGI("Attempting recovery...");
    PCA9555::Status st = device.recover();
    printStatus(st);
    printDriverHealth();
    return;
  }

  if (cmd == "verbose") {
    printVerboseState();
    return;
  }

  if (cmd.startsWith("verbose ")) {
    const char* cursor = cmd.c_str() + 8;
    bool enabled = false;
    if (!parseBinaryToken(cursor, enabled) || hasTrailingArgs(cursor)) {
      LOGW("Usage: verbose <0|1>");
      return;
    }
    verboseMode = enabled;
    LOGI("Verbose mode: %s%s%s",
         onOffColor(verboseMode),
         verboseMode ? "ON" : "OFF",
         LOG_COLOR_RESET);
    return;
  }

  if (cmd == "selftest") {
    runSelfTest();
    return;
  }

  if (cmd.startsWith("sweep")) {
    if (cmd.length() > 5) {
      cmdSweep(cmd.substring(5));
    } else {
      cmdSweep("");
    }
    return;
  }

  if (cmd.startsWith("walk")) {
    if (cmd.length() > 4) {
      cmdWalk(cmd.substring(4));
    } else {
      cmdWalk("");
    }
    return;
  }

  if (cmd == "allhigh") {
    cmdAllHigh();
    return;
  }

  if (cmd == "alllow") {
    cmdAllLow();
    return;
  }

  if (cmd == "stress_mix") {
    runStressMix(50);
    return;
  }

  if (cmd.startsWith("stress_mix ")) {
    const char* cursor = cmd.c_str() + 11;
    long parsedCount = 0;
    if (!parseLongToken(cursor, parsedCount) ||
        hasTrailingArgs(cursor) ||
        parsedCount <= 0 ||
        parsedCount > std::numeric_limits<int>::max()) {
      LOGW("Usage: stress_mix <positive count>");
      return;
    }
    const int count = static_cast<int>(parsedCount);
    runStressMix(count);
    return;
  }

  if (cmd == "stress") {
    stressRemaining = 10;
    resetStressStats(10);
    LOGI("Starting stress test: 10 cycles");
    return;
  }

  if (cmd.startsWith("stress ")) {
    const char* cursor = cmd.c_str() + 7;
    long parsedCount = 0;
    if (!parseLongToken(cursor, parsedCount) ||
        hasTrailingArgs(cursor) ||
        parsedCount <= 0 ||
        parsedCount > std::numeric_limits<int>::max()) {
      LOGW("Usage: stress <positive count>");
      return;
    }
    const int count = static_cast<int>(parsedCount);
    stressRemaining = count;
    resetStressStats(count);
    LOGI("Starting stress test: %d cycles", count);
    return;
  }

  LOGW("Unknown command: '%s'  (type 'help' or '?')", cmd.c_str());
}

// ============================================================================
// Arduino setup / loop
// ============================================================================

void setup() {
  log_begin(115200);
  LOGI("=== PCA9555 Bringup Example ===");

  if (!board::initI2c()) {
    LOGE("Failed to initialize I2C");
    return;
  }
  LOGI("I2C initialized (SDA=%d, SCL=%d)", board::I2C_SDA, board::I2C_SCL);
  bus_diag::scan();

  PCA9555::Config cfg;
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  cfg.nowMs = exampleNowMs;
  cfg.timeUser = nullptr;
  cfg.i2cAddress = 0x20;
  cfg.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  cfg.offlineThreshold = 5;

  // All pins input (default POR state)
  cfg.configPort0 = 0xFF;
  cfg.configPort1 = 0xFF;
  cfg.outputPort0 = 0xFF;
  cfg.outputPort1 = 0xFF;
  cfg.polarityPort0 = 0x00;
  cfg.polarityPort1 = 0x00;
  cfg.applyInterruptErrata = true;
  PCA9555::Status st = device.begin(cfg);
  if (!st.ok()) {
    LOGE("Init failed!");
    printStatus(st);
    return;
  }
  LOGI("PCA9555 initialized at 0x%02X", cfg.i2cAddress);
  printDriverHealth();
  printHelp();
  Serial.print("> ");
}

void loop() {
  device.tick(millis());

  // Non-blocking stress test execution
  if (stressStats.active && stressRemaining > 0) {
    PCA9555::PortData data;
    PCA9555::Status st = device.readInputs(data);
    if (st.ok()) {
      stressStats.okCount++;
    } else {
      stressStats.failCount++;
      if (!stressStats.hasFailure) {
        stressStats.firstFailure = st;
        stressStats.hasFailure = true;
      }
      stressStats.lastFailure = st;
      LOGV(verboseMode, "  stress cycle fail: %s", errToStr(st.code));
    }
    stressRemaining--;
    if (stressRemaining == 0) {
      finishStressStats();
      Serial.print("> ");
    }
  }

  String inputLine;
  if (cli_shell::readLine(inputLine)) {
    processCommand(inputLine);
    Serial.print("> ");
  }
}

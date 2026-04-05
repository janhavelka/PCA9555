/// @file test_basic.cpp
/// @brief Native contract tests for PCA9555 lifecycle and health behavior.

#include <unity.h>

#include "Arduino.h"
#include "Wire.h"

SerialClass Serial;
TwoWire Wire;

#include "PCA9555/PCA9555.h"
#include "common/I2cTransport.h"

using namespace PCA9555;

namespace {

struct FakeBus {
  uint32_t nowMs = 1000;
  uint32_t writeCalls = 0;
  uint32_t readCalls = 0;

  int readErrorRemaining = 0;
  int writeErrorRemaining = 0;
  Status readError = Status::Error(Err::I2C_ERROR, "forced read error", -1);
  Status writeError = Status::Error(Err::I2C_ERROR, "forced write error", -2);

  // Register shadow state (mirrors PCA9555 POR defaults)
  uint8_t regs[8] = {
    0xFF, 0xFF,  // Input Port 0/1 (read-only, simulated)
    0xFF, 0xFF,  // Output Port 0/1
    0x00, 0x00,  // Polarity Inversion 0/1
    0xFF, 0xFF   // Configuration 0/1
  };
};

Status fakeWrite(uint8_t, const uint8_t* data, size_t len, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->writeCalls++;
  if (data == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write args");
  }
  if (bus->writeErrorRemaining > 0) {
    bus->writeErrorRemaining--;
    return bus->writeError;
  }

  // Apply writes to register shadow
  if (len >= 2) {
    uint8_t reg = data[0];
    if (reg <= 0x07) {
      bus->regs[reg] = data[1];
      // Auto-increment within register pair
      if (len >= 3 && (reg % 2 == 0) && (reg + 1) <= 0x07) {
        bus->regs[reg + 1] = data[2];
      }
    }
  }

  return Status::Ok();
}

Status fakeWriteRead(uint8_t, const uint8_t* txData, size_t txLen, uint8_t* rxData,
                     size_t rxLen, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->readCalls++;
  if (txData == nullptr || txLen == 0 || (rxLen > 0 && rxData == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write-read args");
  }
  if (bus->readErrorRemaining > 0) {
    bus->readErrorRemaining--;
    return bus->readError;
  }

  const uint8_t reg = txData[0];
  for (size_t i = 0; i < rxLen; ++i) {
    uint8_t r = reg + static_cast<uint8_t>(i);
    // Auto-increment within pair: toggle LSB within pair
    if (i > 0) {
      r = (reg & 0xFE) | ((reg + static_cast<uint8_t>(i)) & 0x01);
    }
    if (r <= 0x07) {
      rxData[i] = bus->regs[r];
    } else {
      rxData[i] = 0xFF;
    }
  }

  return Status::Ok();
}

uint32_t fakeNowMs(void* user) {
  return static_cast<FakeBus*>(user)->nowMs;
}

Config makeConfig(FakeBus& bus) {
  Config cfg;
  cfg.i2cWrite = fakeWrite;
  cfg.i2cWriteRead = fakeWriteRead;
  cfg.i2cUser = &bus;
  cfg.nowMs = fakeNowMs;
  cfg.timeUser = &bus;
  cfg.i2cTimeoutMs = 10;
  cfg.offlineThreshold = 3;
  cfg.i2cAddress = 0x20;
  return cfg;
}

}  // namespace

void setUp() {
  setMillis(0);
  Wire._clearEndTransmissionResult();
  Wire._clearRequestFromOverride();
}

void tearDown() {}

// ===========================================================================
// Status tests
// ===========================================================================

void test_status_ok() {
  Status st = Status::Ok();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK), static_cast<uint8_t>(st.code));
}

void test_status_error() {
  Status st = Status::Error(Err::I2C_ERROR, "Test error", 42);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(42, st.detail);
}

// ===========================================================================
// Config defaults
// ===========================================================================

void test_config_defaults() {
  Config cfg;
  TEST_ASSERT_NULL(cfg.i2cWrite);
  TEST_ASSERT_NULL(cfg.i2cWriteRead);
  TEST_ASSERT_EQUAL_HEX8(0x20, cfg.i2cAddress);
  TEST_ASSERT_EQUAL_UINT16(50, cfg.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5, cfg.offlineThreshold);
  TEST_ASSERT_EQUAL_HEX8(0xFF, cfg.configPort0);
  TEST_ASSERT_EQUAL_HEX8(0xFF, cfg.configPort1);
  TEST_ASSERT_EQUAL_HEX8(0xFF, cfg.outputPort0);
  TEST_ASSERT_EQUAL_HEX8(0xFF, cfg.outputPort1);
  TEST_ASSERT_EQUAL_HEX8(0x00, cfg.polarityPort0);
  TEST_ASSERT_EQUAL_HEX8(0x00, cfg.polarityPort1);
  TEST_ASSERT_TRUE(cfg.requireConfigPortDefaults);
  TEST_ASSERT_TRUE(cfg.applyInterruptErrata);
}

// ===========================================================================
// begin() validation
// ===========================================================================

void test_begin_rejects_missing_callbacks() {
  PCA9555::PCA9555 dev;
  Config cfg;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_rejects_invalid_address() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cAddress = 0x30;  // Out of PCA9555 range
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));
}

void test_begin_rejects_zero_timeout() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cTimeoutMs = 0;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));
}

void test_begin_success_sets_ready_and_health() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Status st = dev.begin(makeConfig(bus));
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.isOnline());
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
}

void test_get_settings_snapshot_reflects_runtime_state() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0xAA;
  cfg.outputPort1 = 0x55;
  cfg.configPort0 = 0x0F;
  cfg.configPort1 = 0xF0;
  cfg.polarityPort0 = 0x11;
  cfg.polarityPort1 = 0x22;

  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  const SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_TRUE(snapshot.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(snapshot.state));
  TEST_ASSERT_EQUAL_HEX8(cfg.i2cAddress, snapshot.config.i2cAddress);
  TEST_ASSERT_EQUAL_HEX8(cfg.outputPort0, snapshot.config.outputPort0);
  TEST_ASSERT_EQUAL_HEX8(cfg.outputPort1, snapshot.config.outputPort1);
  TEST_ASSERT_EQUAL_HEX8(cfg.configPort0, snapshot.config.configPort0);
  TEST_ASSERT_EQUAL_HEX8(cfg.configPort1, snapshot.config.configPort1);
  TEST_ASSERT_EQUAL_HEX8(cfg.polarityPort0, snapshot.config.polarityPort0);
  TEST_ASSERT_EQUAL_HEX8(cfg.polarityPort1, snapshot.config.polarityPort1);
  TEST_ASSERT_EQUAL_UINT32(0u, snapshot.totalFailures);
  TEST_ASSERT_EQUAL_UINT32(0u, snapshot.totalSuccess);
}

void test_begin_rejects_non_default_config_ports_by_default() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  bus.regs[cmd::REG_CONFIG_PORT_0] = 0xFE;

  Status st = dev.begin(makeConfig(bus));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::CONFIG_REG_MISMATCH),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
}

void test_begin_allows_non_default_config_ports_when_check_disabled() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.requireConfigPortDefaults = false;
  bus.regs[cmd::REG_CONFIG_PORT_0] = 0xFE;
  bus.regs[cmd::REG_CONFIG_PORT_1] = 0xEF;

  Status st = dev.begin(cfg);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(cfg.configPort0, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(cfg.configPort1, bus.regs[cmd::REG_CONFIG_PORT_1]);
}

void test_begin_applies_config_to_device() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0xAA;
  cfg.outputPort1 = 0x55;
  cfg.configPort0 = 0x0F;
  cfg.configPort1 = 0xF0;
  cfg.polarityPort0 = 0x11;
  cfg.polarityPort1 = 0x22;

  Status st = dev.begin(cfg);
  TEST_ASSERT_TRUE(st.ok());

  // Verify the FakeBus shadow received the writes
  TEST_ASSERT_EQUAL_HEX8(0xAA, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x55, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.regs[cmd::REG_CONFIG_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(0x11, bus.regs[cmd::REG_POLARITY_INV_0]);
  TEST_ASSERT_EQUAL_HEX8(0x22, bus.regs[cmd::REG_POLARITY_INV_1]);
}

// ===========================================================================
// millis() fallback
// ===========================================================================

void test_now_ms_fallback_uses_millis_when_callback_missing() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.nowMs = nullptr;
  cfg.timeUser = nullptr;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  setMillis(4321);
  Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(4321u, dev.lastOkMs());
}

// ===========================================================================
// probe() / recover()
// ===========================================================================

void test_probe_failure_does_not_update_health() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t beforeSuccess = dev.totalSuccess();
  const uint32_t beforeFailures = dev.totalFailures();
  const DriverState beforeState = dev.state();

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced probe error", -7);
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(beforeSuccess, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(beforeFailures, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(beforeState),
                          static_cast<uint8_t>(dev.state()));
}

void test_recover_failure_updates_health_once() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced recover error", -8);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(dev.lastError().code));
  TEST_ASSERT_EQUAL_UINT32(bus.nowMs, dev.lastErrorMs());
}

void test_recover_success_returns_ready() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced recover error", -9);
  (void)dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));

  bus.nowMs = 4321;
  Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(4321u, dev.lastOkMs());
}

void test_recover_preserves_transport_error_code() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_NACK_ADDR, "forced recover nack", 7);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(dev.lastError().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
}

void test_recover_reaches_offline_when_threshold_is_one() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced timeout", -10);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_FALSE(dev.isOnline());
}

// ===========================================================================
// Example transport tests
// ===========================================================================

void test_example_transport_maps_wire_errors() {
  Wire._clearEndTransmissionResult();
  Wire._clearRequestFromOverride();

  TEST_ASSERT_TRUE(transport::initWire(8, 9, 400000, 77));
  TEST_ASSERT_EQUAL_UINT32(77u, Wire.getTimeOut());

  const uint8_t byte = 0x55;

  Wire._setEndTransmissionResult(2);
  Status st = transport::wireWrite(0x20, &byte, 1, 123, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(77u, Wire.getTimeOut());

  Wire._setEndTransmissionResult(3);
  st = transport::wireWrite(0x20, &byte, 1, 999, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(77u, Wire.getTimeOut());

  Wire._setEndTransmissionResult(4);
  st = transport::wireWrite(0x20, &byte, 1, 999, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));

  Wire._setEndTransmissionResult(5);
  st = transport::wireWrite(0x20, &byte, 1, 999, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
}

void test_example_transport_validates_params() {
  const uint8_t tx = 0x00;
  uint8_t rx = 0;

  Status st = transport::wireWrite(0x20, nullptr, 1, 50, nullptr);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));

  st = transport::wireWrite(0x20, &tx, 0, 50, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  st = transport::wireWriteRead(0x20, nullptr, 1, &rx, 1, 50, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  st = transport::wireWriteRead(0x20, &tx, 1, nullptr, 1, 50, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

// ===========================================================================
// Input/Output/Config API
// ===========================================================================

void test_read_inputs_returns_port_data() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  bus.regs[0] = 0xAB;  // Input Port 0
  bus.regs[1] = 0xCD;  // Input Port 1
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xAB, data.port0);
  TEST_ASSERT_EQUAL_HEX8(0xCD, data.port1);
  TEST_ASSERT_EQUAL_HEX16(0xCDAB, data.combined());
}

void test_read_inputs_applies_errata_workaround_when_enabled() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = true;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;

  PortData data;
  TEST_ASSERT_TRUE(dev.readInputs(data).ok());

  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 1u, bus.readCalls);
}

void test_read_inputs_skips_errata_workaround_when_disabled() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;

  PortData data;
  TEST_ASSERT_TRUE(dev.readInputs(data).ok());

  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 1u, bus.readCalls);
}

void test_read_register_input_port_applies_errata_workaround() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t writesBefore = bus.writeCalls;
  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readRegister(cmd::REG_INPUT_PORT_0, value).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1u, bus.writeCalls);
}

void test_read_pin_returns_correct_bit() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  bus.regs[0] = 0x04;  // Input Port 0: bit 2 set
  bus.regs[1] = 0x00;  // Input Port 1: all clear
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bool state = false;
  TEST_ASSERT_TRUE(dev.readPin(2, state).ok());
  TEST_ASSERT_TRUE(state);

  TEST_ASSERT_TRUE(dev.readPin(0, state).ok());
  TEST_ASSERT_FALSE(state);

  TEST_ASSERT_TRUE(dev.readPin(3, state).ok());
  TEST_ASSERT_FALSE(state);
}

void test_read_pin_rejects_invalid_pin() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bool state = false;
  Status st = dev.readPin(16, state);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

void test_single_pin_helpers_reject_invalid_pin() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bool flag = false;
  Status st = dev.readOutputPin(16, flag);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  st = dev.getPinPolarity(16, flag);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  st = dev.getPinDirection(16, flag);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

void test_write_outputs_updates_device() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  PortData data;
  data.port0 = 0xAA;
  data.port1 = 0x55;
  Status st = dev.writeOutputs(data);
  TEST_ASSERT_TRUE(st.ok());

  TEST_ASSERT_EQUAL_HEX8(0xAA, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x55, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_bulk_register_helpers_round_trip_and_update_shadow() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint8_t bulkOut[2] = {0xA0, 0x5A};
  Status st = dev.writeRegisters(cmd::REG_OUTPUT_PORT_0, bulkOut, 2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xA0, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x5A, bus.regs[cmd::REG_OUTPUT_PORT_1]);

  uint8_t outReadback[2] = {};
  TEST_ASSERT_TRUE(dev.readRegisters(cmd::REG_OUTPUT_PORT_0, outReadback, 2).ok());
  TEST_ASSERT_EQUAL_HEX8(bulkOut[0], outReadback[0]);
  TEST_ASSERT_EQUAL_HEX8(bulkOut[1], outReadback[1]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.readRegisters(cmd::REG_OUTPUT_PORT_1,
                                                                  outReadback,
                                                                  2).code));

  TEST_ASSERT_TRUE(dev.writePin(0, true).ok());
  TEST_ASSERT_EQUAL_HEX8(0xA1, bus.regs[cmd::REG_OUTPUT_PORT_0]);

  const uint8_t bulkCfg[2] = {0x0F, 0xF0};
  TEST_ASSERT_TRUE(dev.writeRegisters(cmd::REG_CONFIG_PORT_0, bulkCfg, 2).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.writeRegisters(cmd::REG_CONFIG_PORT_1,
                                                                   bulkCfg,
                                                                   2).code));
  const SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_EQUAL_HEX8(bulkCfg[0], snapshot.config.configPort0);
  TEST_ASSERT_EQUAL_HEX8(bulkCfg[1], snapshot.config.configPort1);
}

void test_bulk_read_input_registers_applies_errata_workaround() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = true;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  const uint32_t writesBefore = bus.writeCalls;
  uint8_t inputRegs[2] = {};
  TEST_ASSERT_TRUE(dev.readRegisters(cmd::REG_INPUT_PORT_0, inputRegs, 2).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1u, bus.writeCalls);
}

void test_write_pin_modifies_single_bit() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0xFF;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Clear bit 3 of port 0
  Status st = dev.writePin(3, false);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.regs[cmd::REG_OUTPUT_PORT_0]);

  // Set bit 3 back
  st = dev.writePin(3, true);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_0]);
}

void test_write_pin_port1() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Set bit 0 of port 1 (pin 8)
  Status st = dev.writePin(8, true);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x01, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_read_output_and_output_pin_return_latched_state() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  bus.regs[cmd::REG_OUTPUT_PORT_0] = 0xA5;
  bus.regs[cmd::REG_OUTPUT_PORT_1] = 0x5A;
  Config cfg = makeConfig(bus);
  cfg.requireConfigPortDefaults = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.regs[cmd::REG_OUTPUT_PORT_0] = 0xA5;
  bus.regs[cmd::REG_OUTPUT_PORT_1] = 0x5A;

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readOutput(Port::PORT_0, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0xA5, value);

  bool high = false;
  TEST_ASSERT_TRUE(dev.readOutputPin(15, high).ok());
  TEST_ASSERT_FALSE(high);
  TEST_ASSERT_TRUE(dev.readOutputPin(14, high).ok());
  TEST_ASSERT_TRUE(high);
}

void test_write_pin_no_op_if_already_set() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0xFF;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  uint32_t writesBefore = bus.writeCalls;
  // Pin 3 is already high (0xFF), so writePin(3, true) should be a no-op
  Status st = dev.writePin(3, true);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
}

void test_set_configuration_updates_device() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  PortData data;
  data.port0 = 0x0F;
  data.port1 = 0xF0;
  Status st = dev.setConfiguration(data);
  TEST_ASSERT_TRUE(st.ok());

  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.regs[cmd::REG_CONFIG_PORT_1]);
}

void test_set_pin_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.configPort0 = 0xFF;  // all inputs
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Set pin 2 to output
  Status st = dev.setPinDirection(2, false);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.regs[cmd::REG_CONFIG_PORT_0]);

  // Set pin 2 back to input
  st = dev.setPinDirection(2, true);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_get_port_configuration_and_pin_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.requireConfigPortDefaults = false;
  bus.regs[cmd::REG_CONFIG_PORT_0] = 0xF0;
  bus.regs[cmd::REG_CONFIG_PORT_1] = 0x0F;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.regs[cmd::REG_CONFIG_PORT_0] = 0xF0;
  bus.regs[cmd::REG_CONFIG_PORT_1] = 0x0F;

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.getPortConfiguration(Port::PORT_1, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x0F, value);

  bool input = false;
  TEST_ASSERT_TRUE(dev.getPinDirection(2, input).ok());
  TEST_ASSERT_FALSE(input);
  TEST_ASSERT_TRUE(dev.getPinDirection(11, input).ok());
  TEST_ASSERT_TRUE(input);
}

void test_set_polarity() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  PortData data;
  data.port0 = 0xFF;
  data.port1 = 0x0F;
  Status st = dev.setPolarity(data);
  TEST_ASSERT_TRUE(st.ok());

  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_POLARITY_INV_0]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_POLARITY_INV_1]);
}

void test_set_pin_polarity() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  Status st = dev.setPinPolarity(9, true);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x02, bus.regs[cmd::REG_POLARITY_INV_1]);

  st = dev.setPinPolarity(9, false);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.regs[cmd::REG_POLARITY_INV_1]);
}

void test_get_port_polarity_and_pin_polarity() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.requireConfigPortDefaults = false;
  bus.regs[cmd::REG_POLARITY_INV_0] = 0x11;
  bus.regs[cmd::REG_POLARITY_INV_1] = 0x88;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.regs[cmd::REG_POLARITY_INV_0] = 0x11;
  bus.regs[cmd::REG_POLARITY_INV_1] = 0x88;

  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.getPortPolarity(Port::PORT_0, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x11, value);

  bool inverted = false;
  TEST_ASSERT_TRUE(dev.getPinPolarity(15, inverted).ok());
  TEST_ASSERT_TRUE(inverted);
  TEST_ASSERT_TRUE(dev.getPinPolarity(4, inverted).ok());
  TEST_ASSERT_TRUE(inverted);
  TEST_ASSERT_TRUE(dev.getPinPolarity(5, inverted).ok());
  TEST_ASSERT_FALSE(inverted);
}

void test_read_register_public() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t value = 0;
  Status st = dev.readRegister(cmd::REG_CONFIG_PORT_0, value);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, value);
}

void test_write_register_public_rejects_input_port() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  // Input registers (0x00, 0x01) are not writable
  Status st = dev.writeRegister(0x00, 0x55);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  st = dev.writeRegister(0x01, 0x55);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

void test_port_apis_reject_invalid_port_enum() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const Port invalidPort = static_cast<Port>(2);
  uint8_t value = 0;

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.readInput(invalidPort, value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.writeOutput(invalidPort, 0x55).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.readOutput(invalidPort, value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.setPortConfiguration(invalidPort, 0xAA).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.getPortConfiguration(invalidPort, value).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.setPortPolarity(invalidPort, 0x0F).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.getPortPolarity(invalidPort, value).code));
}

void test_write_register_updates_output_shadow_for_write_pin() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeRegister(cmd::REG_OUTPUT_PORT_0, 0x00).ok());
  TEST_ASSERT_TRUE(dev.writePin(0, true).ok());
  TEST_ASSERT_EQUAL_HEX8(0x01, bus.regs[cmd::REG_OUTPUT_PORT_0]);
}

void test_write_register_updates_config_shadow_for_set_pin_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  TEST_ASSERT_TRUE(dev.writeRegister(cmd::REG_CONFIG_PORT_0, 0x00).ok());
  TEST_ASSERT_TRUE(dev.setPinDirection(0, true).ok());
  TEST_ASSERT_EQUAL_HEX8(0x01, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_recover_reapplies_runtime_configuration() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  PortData outputs = PortData::fromCombined(0x55AA);
  PortData config = PortData::fromCombined(0xF00F);
  PortData polarity = PortData::fromCombined(0x2211);

  TEST_ASSERT_TRUE(dev.writeOutputs(outputs).ok());
  TEST_ASSERT_TRUE(dev.setConfiguration(config).ok());
  TEST_ASSERT_TRUE(dev.setPolarity(polarity).ok());

  bus.regs[cmd::REG_OUTPUT_PORT_0] = cmd::DEFAULT_OUTPUT;
  bus.regs[cmd::REG_OUTPUT_PORT_1] = cmd::DEFAULT_OUTPUT;
  bus.regs[cmd::REG_POLARITY_INV_0] = cmd::DEFAULT_POLARITY;
  bus.regs[cmd::REG_POLARITY_INV_1] = cmd::DEFAULT_POLARITY;
  bus.regs[cmd::REG_CONFIG_PORT_0] = cmd::DEFAULT_CONFIG;
  bus.regs[cmd::REG_CONFIG_PORT_1] = cmd::DEFAULT_CONFIG;

  TEST_ASSERT_TRUE(dev.recover().ok());

  TEST_ASSERT_EQUAL_HEX8(outputs.port0, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(outputs.port1, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(polarity.port0, bus.regs[cmd::REG_POLARITY_INV_0]);
  TEST_ASSERT_EQUAL_HEX8(polarity.port1, bus.regs[cmd::REG_POLARITY_INV_1]);
  TEST_ASSERT_EQUAL_HEX8(config.port0, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(config.port1, bus.regs[cmd::REG_CONFIG_PORT_1]);
}

void test_register_out_of_range() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  uint8_t value = 0;
  Status st = dev.readRegister(0x08, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  st = dev.writeRegister(0x08, 0x55);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
}

// ===========================================================================
// PortData helper
// ===========================================================================

void test_port_data_combined() {
  PortData data;
  data.port0 = 0x34;
  data.port1 = 0x12;
  TEST_ASSERT_EQUAL_HEX16(0x1234, data.combined());
}

void test_port_data_from_combined() {
  PortData data = PortData::fromCombined(0xABCD);
  TEST_ASSERT_EQUAL_HEX8(0xCD, data.port0);
  TEST_ASSERT_EQUAL_HEX8(0xAB, data.port1);
}

// ===========================================================================
// Not-initialized guard
// ===========================================================================

void test_operations_reject_before_begin() {
  PCA9555::PCA9555 dev;
  PortData data;
  bool state;
  bool flag;
  uint8_t val;
  uint8_t buf[2] = {};

  const SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_FALSE(snapshot.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(snapshot.state));

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readInputs(data).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readInput(Port::PORT_0, val).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readPin(0, state).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeOutputs(data).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeOutput(Port::PORT_0, 0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readOutput(Port::PORT_0, val).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writePin(0, false).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readOutputPin(0, flag).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.setConfiguration(data).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.getPortConfiguration(Port::PORT_0, val).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.getConfiguration(data).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.setPolarity(data).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.getPortPolarity(Port::PORT_0, val).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.getPolarity(data).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.getPinPolarity(0, flag).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.getPinDirection(0, flag).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readRegister(0, val).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readRegisters(2, buf, 2).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeRegister(2, 0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.writeRegisters(2, buf, 2).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.probe().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.recover().code));
}

void test_end_sets_safe_input_state() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.configPort0 = 0x00;
  cfg.configPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  dev.end();

  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_1]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
}

// ===========================================================================
// main
// ===========================================================================

int main() {
  UNITY_BEGIN();

  // Status
  RUN_TEST(test_status_ok);
  RUN_TEST(test_status_error);

  // Config
  RUN_TEST(test_config_defaults);

  // begin() validation
  RUN_TEST(test_begin_rejects_missing_callbacks);
  RUN_TEST(test_begin_rejects_invalid_address);
  RUN_TEST(test_begin_rejects_zero_timeout);
  RUN_TEST(test_begin_success_sets_ready_and_health);
  RUN_TEST(test_get_settings_snapshot_reflects_runtime_state);
  RUN_TEST(test_begin_rejects_non_default_config_ports_by_default);
  RUN_TEST(test_begin_allows_non_default_config_ports_when_check_disabled);
  RUN_TEST(test_begin_applies_config_to_device);

  // millis fallback
  RUN_TEST(test_now_ms_fallback_uses_millis_when_callback_missing);

  // probe / recover / health
  RUN_TEST(test_probe_failure_does_not_update_health);
  RUN_TEST(test_recover_failure_updates_health_once);
  RUN_TEST(test_recover_success_returns_ready);
  RUN_TEST(test_recover_preserves_transport_error_code);
  RUN_TEST(test_recover_reaches_offline_when_threshold_is_one);

  // Example transport
  RUN_TEST(test_example_transport_maps_wire_errors);
  RUN_TEST(test_example_transport_validates_params);

  // Input/Output/Config API
  RUN_TEST(test_read_inputs_returns_port_data);
  RUN_TEST(test_read_inputs_applies_errata_workaround_when_enabled);
  RUN_TEST(test_read_inputs_skips_errata_workaround_when_disabled);
  RUN_TEST(test_read_register_input_port_applies_errata_workaround);
  RUN_TEST(test_read_pin_returns_correct_bit);
  RUN_TEST(test_read_pin_rejects_invalid_pin);
  RUN_TEST(test_single_pin_helpers_reject_invalid_pin);
  RUN_TEST(test_port_apis_reject_invalid_port_enum);
  RUN_TEST(test_write_outputs_updates_device);
  RUN_TEST(test_bulk_register_helpers_round_trip_and_update_shadow);
  RUN_TEST(test_bulk_read_input_registers_applies_errata_workaround);
  RUN_TEST(test_write_pin_modifies_single_bit);
  RUN_TEST(test_write_pin_port1);
  RUN_TEST(test_read_output_and_output_pin_return_latched_state);
  RUN_TEST(test_write_pin_no_op_if_already_set);
  RUN_TEST(test_set_configuration_updates_device);
  RUN_TEST(test_set_pin_direction);
  RUN_TEST(test_get_port_configuration_and_pin_direction);
  RUN_TEST(test_set_polarity);
  RUN_TEST(test_set_pin_polarity);
  RUN_TEST(test_get_port_polarity_and_pin_polarity);
  RUN_TEST(test_read_register_public);
  RUN_TEST(test_write_register_public_rejects_input_port);
  RUN_TEST(test_write_register_updates_output_shadow_for_write_pin);
  RUN_TEST(test_write_register_updates_config_shadow_for_set_pin_direction);
  RUN_TEST(test_register_out_of_range);
  RUN_TEST(test_recover_reapplies_runtime_configuration);

  // PortData
  RUN_TEST(test_port_data_combined);
  RUN_TEST(test_port_data_from_combined);

  // Not-initialized guards
  RUN_TEST(test_operations_reject_before_begin);
  RUN_TEST(test_end_sets_safe_input_state);

  return UNITY_END();
}

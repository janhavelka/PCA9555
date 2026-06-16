/// @file test_basic.cpp
/// @brief Native contract tests for PCA9555 lifecycle and health behavior.

#include <unity.h>
#include <type_traits>

#include "Arduino.h"
#include "Wire.h"

SerialClass Serial;
TwoWire Wire;

#include "PCA9555/PCA9555.h"
#include "common/I2cTransport.h"

using namespace PCA9555;

static_assert(!std::is_copy_constructible<PCA9555::PCA9555>::value,
              "PCA9555 driver instances must not be copy constructible");
static_assert(!std::is_copy_assignable<PCA9555::PCA9555>::value,
              "PCA9555 driver instances must not be copy assignable");
static_assert(!std::is_move_constructible<PCA9555::PCA9555>::value,
              "PCA9555 driver instances must not be move constructible");
static_assert(!std::is_move_assignable<PCA9555::PCA9555>::value,
              "PCA9555 driver instances must not be move assignable");

namespace {

enum class TraceKind : uint8_t {
  WRITE,
  READ
};

struct TraceEntry {
  TraceKind kind = TraceKind::WRITE;
  uint8_t reg = 0;
  uint8_t len = 0;
  uint8_t data0 = 0;
  uint8_t data1 = 0;
};

struct FakeBus {
  static constexpr size_t MAX_TRACE = 64;

  uint32_t nowMs = 1000;
  uint32_t writeCalls = 0;
  uint32_t readCalls = 0;

  int readErrorRemaining = 0;
  int writeErrorRemaining = 0;
  int writeErrorRegister = -1;
  int partialWriteErrorRegister = -1;
  Status readError = Status::Error(Err::I2C_ERROR, "forced read error", -1);
  Status writeError = Status::Error(Err::I2C_ERROR, "forced write error", -2);
  int lockErrorRemaining = 0;
  Status lockError = Status::Error(Err::BUSY, "forced lock error", -3);
  uint32_t lockCalls = 0;
  uint32_t unlockCalls = 0;
  TraceEntry trace[MAX_TRACE] = {};
  size_t traceCount = 0;

  // Register shadow state (mirrors PCA9555 POR defaults)
  uint8_t regs[8] = {
    0xFF, 0xFF,  // Input Port 0/1 (read-only, simulated)
    0xFF, 0xFF,  // Output Port 0/1
    0x00, 0x00,  // Polarity Inversion 0/1
    0xFF, 0xFF   // Configuration 0/1
  };
};

void recordTrace(FakeBus* bus, const TraceEntry& entry) {
  if (bus->traceCount < FakeBus::MAX_TRACE) {
    bus->trace[bus->traceCount] = entry;
  }
  bus->traceCount++;
}

void resetTrace(FakeBus& bus) {
  bus.traceCount = 0;
}

Status fakeWrite(uint8_t, const uint8_t* data, size_t len, uint32_t, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->writeCalls++;
  if (data == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "invalid fake write args");
  }

  TraceEntry entry;
  entry.kind = TraceKind::WRITE;
  entry.reg = data[0];
  entry.len = static_cast<uint8_t>(len);
  entry.data0 = (len > 1) ? data[1] : 0;
  entry.data1 = (len > 2) ? data[2] : 0;
  recordTrace(bus, entry);

  if (bus->writeErrorRemaining > 0) {
    bus->writeErrorRemaining--;
    return bus->writeError;
  }
  if (bus->writeErrorRegister >= 0 && data[0] == static_cast<uint8_t>(bus->writeErrorRegister)) {
    bus->writeErrorRegister = -1;
    return bus->writeError;
  }
  if (bus->partialWriteErrorRegister >= 0 &&
      data[0] == static_cast<uint8_t>(bus->partialWriteErrorRegister) && len >= 3) {
    const uint8_t reg = data[0];
    if (reg <= 0x07) {
      bus->regs[reg] = data[1];
    }
    bus->partialWriteErrorRegister = -1;
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

  TraceEntry entry;
  entry.kind = TraceKind::READ;
  entry.reg = txData[0];
  entry.len = static_cast<uint8_t>(rxLen);
  recordTrace(bus, entry);

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

Status fakeLock(void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->lockCalls++;
  if (bus->lockErrorRemaining > 0) {
    bus->lockErrorRemaining--;
    return bus->lockError;
  }
  return Status::Ok();
}

void fakeUnlock(void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->unlockCalls++;
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
  TEST_ASSERT_NULL(cfg.i2cLock);
  TEST_ASSERT_NULL(cfg.i2cUnlock);
  TEST_ASSERT_NULL(cfg.i2cLockUser);
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

void test_begin_rejects_unpaired_lock_hooks() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cLock = fakeLock;
  cfg.i2cUnlock = nullptr;

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
  TEST_ASSERT_FALSE(snapshot.outputDirty);
  TEST_ASSERT_FALSE(snapshot.polarityDirty);
  TEST_ASSERT_FALSE(snapshot.configDirty);
  TEST_ASSERT_FALSE(dev.hasDirtyState());
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

void test_offline_blocks_public_io_without_backend_transfer() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  PortData ignored;
  (void)dev.readInputs(ignored);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  resetTrace(bus);
  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OFFLINE), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.traceCount);

  st = dev.writeOutputs(PortData::fromCombined(0x0000));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OFFLINE), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.traceCount);

  st = dev.setOutputBits(0xFFFF);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OFFLINE), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.traceCount);

  st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OFFLINE), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.traceCount);

  st = dev.startReadInputsJob();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OFFLINE), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.traceCount);

  st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(bus.traceCount > 0u);
}

void test_apply_interrupt_errata_workaround_uses_lock_hooks() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cLock = fakeLock;
  cfg.i2cUnlock = fakeUnlock;
  cfg.i2cLockUser = &bus;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetTrace(bus);

  Status st = dev.applyInterruptErrataWorkaround();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[0].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_UINT8(1u, bus.trace[0].len);
}

void test_apply_interrupt_errata_workaround_unlocked_skips_lock_hooks() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cLock = fakeLock;
  cfg.i2cUnlock = fakeUnlock;
  cfg.i2cLockUser = &bus;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetTrace(bus);

  Status st = dev.applyInterruptErrataWorkaroundUnlocked();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.trace[0].reg);
}

void test_apply_interrupt_errata_lock_failure_skips_i2c_and_unlock() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cLock = fakeLock;
  cfg.i2cUnlock = fakeUnlock;
  cfg.i2cLockUser = &bus;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetTrace(bus);

  bus.lockErrorRemaining = 1;
  bus.lockError = Status::Error(Err::BUSY, "forced lock busy", -15);
  Status st = dev.applyInterruptErrataWorkaround();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.traceCount);
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
  resetTrace(bus);

  const uint32_t writesBefore = bus.writeCalls;
  const uint32_t readsBefore = bus.readCalls;

  PortData data;
  TEST_ASSERT_TRUE(dev.readInputs(data).ok());

  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore + 1u, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[1].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.trace[1].reg);
  TEST_ASSERT_TRUE(bus.trace[1].reg != 0x00);
  TEST_ASSERT_EQUAL_UINT8(1u, bus.trace[1].len);
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
  resetTrace(bus);

  const uint32_t writesBefore = bus.writeCalls;
  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readRegister(cmd::REG_INPUT_PORT_0, value).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore + 1u, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.trace[1].reg);
  TEST_ASSERT_TRUE(bus.trace[1].reg != 0x00);
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

void test_read_inputs_job_without_errata_budget_1_completes_one_read() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = false;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0x12;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0x34;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(2000, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::READ),
                          static_cast<uint8_t>(bus.trace[0].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_INPUT_PORT_0, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_UINT8(2u, bus.trace[0].len);

  PortData data;
  TEST_ASSERT_TRUE(dev.getLastReadInputs(data).ok());
  TEST_ASSERT_EQUAL_HEX8(0x12, data.port0);
  TEST_ASSERT_EQUAL_HEX8(0x34, data.port1);
  TEST_ASSERT_EQUAL_UINT32(2000u, dev.lastOkMs());
}

void test_read_inputs_job_with_errata_budget_1_splits_read_and_pointer_park() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = true;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0xAB;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0xCD;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(3000, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_TRUE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::READ),
                          static_cast<uint8_t>(bus.trace[0].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_INPUT_PORT_0, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_UINT8(2u, bus.trace[0].len);

  PortData data;
  TEST_ASSERT_TRUE(dev.getLastReadInputs(data).ok());
  TEST_ASSERT_EQUAL_HEX8(0xAB, data.port0);
  TEST_ASSERT_EQUAL_HEX8(0xCD, data.port1);

  st = dev.pollJob(3001, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[1].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.trace[1].reg);
  TEST_ASSERT_EQUAL_UINT8(1u, bus.trace[1].len);
  TEST_ASSERT_EQUAL_UINT32(3001u, dev.lastOkMs());
}

void test_read_inputs_job_lock_hooks_do_not_hide_extra_i2c_instructions() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = true;
  cfg.i2cLock = fakeLock;
  cfg.i2cUnlock = fakeUnlock;
  cfg.i2cLockUser = &bus;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(3500, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::READ),
                          static_cast<uint8_t>(bus.trace[0].kind));

  st = dev.pollJob(3501, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[1].kind));
}

void test_read_inputs_job_with_errata_budget_2_completes_two_instructions() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = true;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(4000, 2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::READ),
                          static_cast<uint8_t>(bus.trace[0].kind));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[1].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.trace[1].reg);
  TEST_ASSERT_EQUAL_UINT8(1u, bus.trace[1].len);
}

void test_read_inputs_job_read_failure_skips_pointer_park() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_TIMEOUT, "forced read timeout", -11);
  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(5000, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::READ),
                          static_cast<uint8_t>(bus.trace[0].kind));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(dev.lastJobStatus().code));
}

void test_read_inputs_job_pointer_park_failure_propagates_write_error() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced park nack", -12);
  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(6000, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[1].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.trace[1].reg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.lastJobStatus().code));
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
                          static_cast<uint8_t>(dev.startReadInputsJob().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startWriteOutputsJob(0x01, 0x01).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startConfigureOutputsJob(0x01, 0x00).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.pollJob(0, 1).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.configureOutputs(0x01, 0x00).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.applyInterruptErrataWorkaround().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.applyInterruptErrataWorkaroundUnlocked().code));
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
// Bit Manipulation API
// ===========================================================================

void test_set_output_bits() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0x00;
  cfg.outputPort1 = 0x00;
  cfg.configPort0 = 0x00;
  cfg.configPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Set bits 0-3 of port 0 and bit 8 of port 1
  Status st = dev.setOutputBits(0x010F);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x01, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_clear_output_bits() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0xFF;
  cfg.outputPort1 = 0xFF;
  cfg.configPort0 = 0x00;
  cfg.configPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Clear bits 4-7 of port 0
  Status st = dev.clearOutputBits(0x00F0);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_toggle_output_bits() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0xAA;
  cfg.outputPort1 = 0x55;
  cfg.configPort0 = 0x00;
  cfg.configPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Toggle all bits
  Status st = dev.toggleOutputBits(0xFFFF);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x55, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xAA, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_toggle_pin_bit_manip() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.configPort0 = 0x00;
  cfg.configPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Toggle pin 3 (port 0 starts at 0xFF)
  Status st = dev.togglePin(3);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xF7, bus.regs[cmd::REG_OUTPUT_PORT_0]);

  // Toggle pin 3 back
  st = dev.togglePin(3);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_0]);

  // Toggle pin 10 (port 1, bit 2)
  st = dev.togglePin(10);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_toggle_pin_rejects_invalid() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.togglePin(16).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(dev.togglePin(255).code));
}

void test_configure_input_bits() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.configPort0 = 0x00;
  cfg.configPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Set pins 0-3 and pin 8 back to input
  Status st = dev.configureInputBits(0x010F);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x01, bus.regs[cmd::REG_CONFIG_PORT_1]);
}

void test_configure_output_bits() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  // Start with all inputs (0xFF default)
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  // Set pins 8-11 to output
  Status st = dev.configureOutputBits(0x0F00);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.regs[cmd::REG_CONFIG_PORT_1]);
}

void test_write_outputs_job_noop_is_cpu_only() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  const uint32_t successBefore = dev.totalSuccess();
  const uint32_t failuresBefore = dev.totalFailures();
  const uint32_t lastOkBefore = dev.lastOkMs();

  Status st = dev.startWriteOutputsJob(0xFFFF, 0xFFFF);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_TRUE(dev.pollJob(7000, 1).ok());
  TEST_ASSERT_EQUAL_UINT32(0u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT32(successBefore, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(failuresBefore, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(lastOkBefore, dev.lastOkMs());
}

void test_write_outputs_job_mask_writes_one_output_pair_instruction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startWriteOutputsJob(0x00F0, 0x0000).inProgress());
  Status st = dev.pollJob(7100, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[0].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_UINT8(3u, bus.trace[0].len);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_configure_outputs_job_budget_1_preloads_latch_before_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startConfigureOutputsJob(0x00F0, 0x0000).inProgress());
  Status st = dev.pollJob(8000, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_TRUE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[0].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_UINT8(3u, bus.trace[0].len);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);

  st = dev.pollJob(8001, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TraceKind::WRITE),
                          static_cast<uint8_t>(bus.trace[1].kind));
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.trace[1].reg);
  TEST_ASSERT_EQUAL_UINT8(3u, bus.trace[1].len);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_configure_outputs_job_budget_2_writes_latch_then_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startConfigureOutputsJob(0x0F00, 0x0000).inProgress());
  Status st = dev.pollJob(8100, 2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.trace[1].reg);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.regs[cmd::REG_CONFIG_PORT_1]);
}

void test_configure_outputs_job_latch_failure_skips_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_BUS, "forced latch write failure", -13);
  TEST_ASSERT_TRUE(dev.startConfigureOutputsJob(0x00F0, 0x0000).inProgress());
  Status st = dev.pollJob(8200, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_configure_outputs_job_direction_failure_propagates_after_latch() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startConfigureOutputsJob(0x00F0, 0x0000).inProgress());
  TEST_ASSERT_TRUE(dev.pollJob(8300, 1).inProgress());
  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced config write failure", -14);
  Status st = dev.pollJob(8301, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.trace[1].reg);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_configure_outputs_blocking_preloads_before_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  Status st = dev.configureOutputs(0x00F0, 0x0000);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.trace[0].reg);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.trace[1].reg);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_configure_outputs_blocking_direction_failure_marks_config_dirty() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  bus.writeErrorRegister = cmd::REG_CONFIG_PORT_0;
  Status st = dev.configureOutputs(0x00F0, 0x0000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(2u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);

  SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_FALSE(snapshot.outputDirty);
  TEST_ASSERT_TRUE(snapshot.configDirty);

  resetTrace(bus);
  st = dev.configureOutputs(0x00F0, 0x0000);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.trace[0].reg);
  snapshot = dev.getSettings();
  TEST_ASSERT_FALSE(snapshot.configDirty);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_active_job_blocks_synchronous_i2c_helpers() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  PortData outputs = PortData::fromCombined(0x0000);
  Status st = dev.writeOutputs(outputs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.traceCount);

  TEST_ASSERT_TRUE(dev.pollJob(8400, 2).ok());
}

void test_set_invert_bits() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  // Enable inversion for pins 0 and 8
  Status st = dev.setInvertBits(0x0101);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x01, bus.regs[cmd::REG_POLARITY_INV_0]);
  TEST_ASSERT_EQUAL_HEX8(0x01, bus.regs[cmd::REG_POLARITY_INV_1]);
}

void test_clear_invert_bits() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.polarityPort0 = 0xFF;
  cfg.polarityPort1 = 0xFF;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  // Disable inversion for pins 0-7 (port 0)
  Status st = dev.clearInvertBits(0x00FF);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.regs[cmd::REG_POLARITY_INV_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_POLARITY_INV_1]);
}

void test_bit_manipulation_no_op_skips_i2c() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  // Outputs start at 0xFF (default)
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint32_t writesBefore = bus.writeCalls;

  // setOutputBits with all bits already high -- no-op
  TEST_ASSERT_TRUE(dev.setOutputBits(0xFFFF).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);

  // clearOutputBits(0) -- no bits to clear -- no-op
  TEST_ASSERT_TRUE(dev.clearOutputBits(0x0000).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);

  // toggleOutputBits(0) -- no bits to toggle -- no-op
  TEST_ASSERT_TRUE(dev.toggleOutputBits(0x0000).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);

  // configureInputBits with all pins already input -- no-op
  TEST_ASSERT_TRUE(dev.configureInputBits(0xFFFF).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);

  // configureOutputBits(0) -- no bits to change -- no-op
  TEST_ASSERT_TRUE(dev.configureOutputBits(0x0000).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);

  // setInvertBits(0) -- no-op
  TEST_ASSERT_TRUE(dev.setInvertBits(0x0000).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);

  // clearInvertBits with all polarity already 0 -- no-op
  TEST_ASSERT_TRUE(dev.clearInvertBits(0xFFFF).ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
}

void test_output_dirty_state_reapplies_cached_noop_write() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  bus.partialWriteErrorRegister = cmd::REG_OUTPUT_PORT_0;
  Status st = dev.clearOutputBits(0x0001);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0xFE, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_TRUE(snapshot.outputDirty);
  TEST_ASSERT_TRUE(dev.hasDirtyState());

  resetTrace(bus);
  st = dev.setOutputBits(0xFFFF);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.trace[0].reg);
  snapshot = dev.getSettings();
  TEST_ASSERT_FALSE(snapshot.outputDirty);
  TEST_ASSERT_FALSE(dev.hasDirtyState());
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_config_and_polarity_dirty_state_reapply_cached_noop_writes() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetTrace(bus);

  bus.writeErrorRegister = cmd::REG_CONFIG_PORT_0;
  Status st = dev.configureOutputBits(0x0001);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_TRUE(snapshot.configDirty);

  resetTrace(bus);
  st = dev.configureInputBits(0xFFFF);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.trace[0].reg);
  snapshot = dev.getSettings();
  TEST_ASSERT_FALSE(snapshot.configDirty);

  bus.writeErrorRegister = cmd::REG_POLARITY_INV_0;
  st = dev.setInvertBits(0x0001);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  snapshot = dev.getSettings();
  TEST_ASSERT_TRUE(snapshot.polarityDirty);

  resetTrace(bus);
  st = dev.clearInvertBits(0xFFFF);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.traceCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_POLARITY_INV_0, bus.trace[0].reg);
  snapshot = dev.getSettings();
  TEST_ASSERT_FALSE(snapshot.polarityDirty);
}

void test_bit_manipulation_rejects_before_begin() {
  PCA9555::PCA9555 dev;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.setOutputBits(0x01).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.clearOutputBits(0x01).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.toggleOutputBits(0x01).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.togglePin(0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.configureInputBits(0x01).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.configureOutputBits(0x01).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.setInvertBits(0x01).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.clearInvertBits(0x01).code));
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
  RUN_TEST(test_begin_rejects_unpaired_lock_hooks);
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
  RUN_TEST(test_offline_blocks_public_io_without_backend_transfer);
  RUN_TEST(test_apply_interrupt_errata_workaround_uses_lock_hooks);
  RUN_TEST(test_apply_interrupt_errata_workaround_unlocked_skips_lock_hooks);
  RUN_TEST(test_apply_interrupt_errata_lock_failure_skips_i2c_and_unlock);

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
  RUN_TEST(test_read_inputs_job_without_errata_budget_1_completes_one_read);
  RUN_TEST(test_read_inputs_job_with_errata_budget_1_splits_read_and_pointer_park);
  RUN_TEST(test_read_inputs_job_lock_hooks_do_not_hide_extra_i2c_instructions);
  RUN_TEST(test_read_inputs_job_with_errata_budget_2_completes_two_instructions);
  RUN_TEST(test_read_inputs_job_read_failure_skips_pointer_park);
  RUN_TEST(test_read_inputs_job_pointer_park_failure_propagates_write_error);
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

  // Bit Manipulation API
  RUN_TEST(test_set_output_bits);
  RUN_TEST(test_clear_output_bits);
  RUN_TEST(test_toggle_output_bits);
  RUN_TEST(test_toggle_pin_bit_manip);
  RUN_TEST(test_toggle_pin_rejects_invalid);
  RUN_TEST(test_configure_input_bits);
  RUN_TEST(test_configure_output_bits);
  RUN_TEST(test_write_outputs_job_noop_is_cpu_only);
  RUN_TEST(test_write_outputs_job_mask_writes_one_output_pair_instruction);
  RUN_TEST(test_configure_outputs_job_budget_1_preloads_latch_before_direction);
  RUN_TEST(test_configure_outputs_job_budget_2_writes_latch_then_direction);
  RUN_TEST(test_configure_outputs_job_latch_failure_skips_direction);
  RUN_TEST(test_configure_outputs_job_direction_failure_propagates_after_latch);
  RUN_TEST(test_configure_outputs_blocking_preloads_before_direction);
  RUN_TEST(test_configure_outputs_blocking_direction_failure_marks_config_dirty);
  RUN_TEST(test_active_job_blocks_synchronous_i2c_helpers);
  RUN_TEST(test_set_invert_bits);
  RUN_TEST(test_clear_invert_bits);
  RUN_TEST(test_bit_manipulation_no_op_skips_i2c);
  RUN_TEST(test_output_dirty_state_reapplies_cached_noop_write);
  RUN_TEST(test_config_and_polarity_dirty_state_reapply_cached_noop_writes);
  RUN_TEST(test_bit_manipulation_rejects_before_begin);

  // PortData
  RUN_TEST(test_port_data_combined);
  RUN_TEST(test_port_data_from_combined);

  // Not-initialized guards
  RUN_TEST(test_operations_reject_before_begin);
  RUN_TEST(test_end_sets_safe_input_state);

  return UNITY_END();
}

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

static_assert(!std::is_copy_constructible<::PCA9555::PCA9555>::value,
              "PCA9555 driver instances must not be copy constructible");
static_assert(!std::is_copy_assignable<::PCA9555::PCA9555>::value,
              "PCA9555 driver instances must not be copy assignable");
static_assert(!std::is_move_constructible<::PCA9555::PCA9555>::value,
              "PCA9555 driver instances must not be move constructible");
static_assert(!std::is_move_assignable<::PCA9555::PCA9555>::value,
              "PCA9555 driver instances must not be move assignable");

namespace {

static constexpr size_t FAKE_LOG_CAPACITY = 128;
static constexpr size_t FAKE_BUF_CAPACITY = 8;

struct FakeTransaction {
  uint8_t type = 0;
  uint8_t address = 0;
  uint32_t timeoutMs = 0;
  uint8_t tx[FAKE_BUF_CAPACITY] = {};
  size_t txLen = 0;
  uint8_t rx[FAKE_BUF_CAPACITY] = {};
  size_t rxRequested = 0;
  size_t rxLen = 0;
  Status status = Status::Ok();
  uint8_t pointerBefore = 0;
  uint8_t pointerAfter = 0;
  size_t dataBytesReachedHardware = 0;
};

struct FakeBus {
  uint32_t nowMs = 1000;
  uint32_t writeCalls = 0;
  uint32_t readCalls = 0;

  uint8_t deviceAddress = cmd::BASE_ADDRESS;
  bool devicePresent = true;
  bool enforceAddress = false;
  Status addressError = Status::Error(Err::I2C_NACK_ADDR, "fake address not acknowledged", -5);
  int readErrorRemaining = 0;
  int writeErrorRemaining = 0;
  uint32_t writeErrorOnCall = 0;
  int partialWriteErrorRemaining = 0;
  size_t partialWriteDataBytesBeforeError = 0;
  int shortWriteErrorRemaining = 0;
  size_t shortWriteBytesBeforeError = 0;
  int shortReadErrorRemaining = 0;
  size_t shortReadBytesBeforeError = 0;
  Status readError = Status::Error(Err::I2C_ERROR, "forced read error", -1);
  Status writeError = Status::Error(Err::I2C_ERROR, "forced write error", -2);
  Status partialWriteError = Status::Error(Err::I2C_NACK_DATA, "forced partial write", -3);
  Status shortWriteError = Status::Error(Err::I2C_NACK_DATA, "forced short write", -4);
  Status shortReadError = Status::Error(Err::I2C_ERROR, "forced short read", -6);
  int lockErrorRemaining = 0;
  Status lockError = Status::Error(Err::BUSY, "forced lock error", -7);
  bool anyDataByteReachedHardware = false;
  size_t lastWriteDataBytesReachedHardware = 0;
  uint32_t totalDataBytesReachedHardware = 0;
  uint8_t commandPointer = 0;
  bool intPending0 = false;
  bool intPending1 = false;
  uint32_t lockCalls = 0;
  uint32_t unlockCalls = 0;
  bool lockHeld = false;
  bool interleaverEnabled = false;
  uint32_t interleaverAttempts = 0;
  uint32_t interleaverRan = 0;
  uint32_t interleaverBlocked = 0;
  size_t transactionCount = 0;
  uint8_t transactionType[FAKE_LOG_CAPACITY] = {};
  uint8_t transactionReg[FAKE_LOG_CAPACITY] = {};
  uint8_t transactionData0[FAKE_LOG_CAPACITY] = {};
  uint8_t transactionData1[FAKE_LOG_CAPACITY] = {};
  size_t transactionLen[FAKE_LOG_CAPACITY] = {};
  FakeTransaction transactions[FAKE_LOG_CAPACITY] = {};

  // Register shadow state. Writable registers mirror PCA9555 POR defaults;
  // input registers model external pin sense and are not an identity/default source.
  uint8_t regs[8] = {
    0xFF, 0xFF,  // Input Port 0/1 (read-only, simulated)
    0xFF, 0xFF,  // Output Port 0/1
    0x00, 0x00,  // Polarity Inversion 0/1
    0xFF, 0xFF   // Configuration 0/1
  };
};

uint8_t fakePairedRegisterAt(uint8_t startReg, size_t offset) {
  return static_cast<uint8_t>((startReg & 0xFEU) |
                              ((startReg + static_cast<uint8_t>(offset)) & 0x01U));
}

void copyFakeBytes(uint8_t* dst, size_t dstCapacity, const uint8_t* src, size_t len) {
  if (dst == nullptr || src == nullptr) {
    return;
  }
  const size_t copyLen = (len < dstCapacity) ? len : dstCapacity;
  for (size_t i = 0; i < copyLen; ++i) {
    dst[i] = src[i];
  }
}

uint8_t fakeInputPolarityReg(uint8_t inputReg) {
  return (inputReg == cmd::REG_INPUT_PORT_0)
      ? cmd::REG_POLARITY_INV_0
      : cmd::REG_POLARITY_INV_1;
}

uint8_t fakeReadableRegisterValue(const FakeBus* bus, uint8_t reg) {
  if (reg == cmd::REG_INPUT_PORT_0 || reg == cmd::REG_INPUT_PORT_1) {
    return static_cast<uint8_t>(bus->regs[reg] ^ bus->regs[fakeInputPolarityReg(reg)]);
  }
  if (reg <= cmd::REG_CONFIG_PORT_1) {
    return bus->regs[reg];
  }
  return 0xFF;
}

Status fakeCheckAddress(const FakeBus* bus, uint8_t addr) {
  if (!bus->devicePresent || (bus->enforceAddress && addr != bus->deviceAddress)) {
    return bus->addressError;
  }
  return Status::Ok();
}

size_t beginFakeTransaction(FakeBus* bus, uint8_t type, uint8_t address,
                            uint32_t timeoutMs, const uint8_t* tx, size_t txLen,
                            size_t rxRequested, size_t legacyLen) {
  if (bus->transactionCount >= FAKE_LOG_CAPACITY) {
    return FAKE_LOG_CAPACITY;
  }
  const size_t idx = bus->transactionCount++;
  FakeTransaction& transaction = bus->transactions[idx];
  transaction = FakeTransaction{};
  transaction.type = type;
  transaction.address = address;
  transaction.timeoutMs = timeoutMs;
  transaction.txLen = (txLen < FAKE_BUF_CAPACITY) ? txLen : FAKE_BUF_CAPACITY;
  transaction.rxRequested = rxRequested;
  transaction.status = Status::Ok();
  transaction.pointerBefore = bus->commandPointer;
  transaction.pointerAfter = bus->commandPointer;
  copyFakeBytes(transaction.tx, FAKE_BUF_CAPACITY, tx, txLen);

  bus->transactionType[idx] = type;
  bus->transactionLen[idx] = legacyLen;
  bus->transactionReg[idx] = (tx != nullptr && txLen > 0) ? tx[0] : 0;
  bus->transactionData0[idx] = (tx != nullptr && txLen > 1) ? tx[1] : 0;
  bus->transactionData1[idx] = (tx != nullptr && txLen > 2) ? tx[2] : 0;
  return idx;
}

Status finishFakeTransaction(FakeBus* bus, size_t idx, const Status& status,
                             const uint8_t* rx = nullptr, size_t rxLen = 0,
                             size_t dataBytesReachedHardware = 0) {
  if (idx < FAKE_LOG_CAPACITY) {
    FakeTransaction& transaction = bus->transactions[idx];
    transaction.status = status;
    transaction.pointerAfter = bus->commandPointer;
    transaction.rxLen = (rxLen < FAKE_BUF_CAPACITY) ? rxLen : FAKE_BUF_CAPACITY;
    transaction.dataBytesReachedHardware = dataBytesReachedHardware;
    copyFakeBytes(transaction.rx, FAKE_BUF_CAPACITY, rx, rxLen);
  }
  return status;
}

void recordFakeEvent(FakeBus* bus, uint8_t type, uint8_t reg, size_t len,
                     uint8_t data0 = 0, uint8_t data1 = 0) {
  if (bus->transactionCount >= FAKE_LOG_CAPACITY) {
    return;
  }
  const uint8_t tx[3] = {reg, data0, data1};
  const size_t txLen = (len > 2) ? 3 : ((len > 0) ? len : 0);
  const size_t idx = beginFakeTransaction(bus, type, 0, 0, tx, txLen, 0, len);
  bus->transactionReg[idx] = reg;
  bus->transactionData0[idx] = data0;
  bus->transactionData1[idx] = data1;
  (void)finishFakeTransaction(bus, idx, Status::Ok());
}

void clearFakeInterruptsForRead(FakeBus* bus, uint8_t startReg, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const uint8_t reg = fakePairedRegisterAt(startReg, i);
    if (reg == cmd::REG_INPUT_PORT_0) {
      bus->intPending0 = false;
    } else if (reg == cmd::REG_INPUT_PORT_1) {
      bus->intPending1 = false;
    }
  }
}

void attemptFakeInterleaver(FakeBus* bus) {
  if (!bus->interleaverEnabled) {
    return;
  }
  bus->interleaverEnabled = false;
  bus->interleaverAttempts++;
  if (bus->lockHeld) {
    bus->interleaverBlocked++;
    return;
  }
  bus->interleaverRan++;
  recordFakeEvent(bus, static_cast<uint8_t>('I'), 0x7E, 1);
}

size_t applyFakeWriteData(FakeBus* bus, const uint8_t* data, size_t len,
                          size_t maxDataBytes) {
  if (len < 2) {
    bus->lastWriteDataBytesReachedHardware = 0;
    return 0;
  }

  const uint8_t reg = data[0];
  if (reg > 0x07) {
    bus->lastWriteDataBytesReachedHardware = 0;
    return 0;
  }

  const size_t availableDataBytes = len - 1;
  const size_t bytesToApply =
      (maxDataBytes < availableDataBytes) ? maxDataBytes : availableDataBytes;
  for (size_t i = 1; i <= bytesToApply; ++i) {
    const uint8_t targetReg = static_cast<uint8_t>((reg & 0xFEU) |
        ((reg + static_cast<uint8_t>(i - 1U)) & 0x01U));
    if (targetReg == cmd::REG_INPUT_PORT_0 || targetReg == cmd::REG_INPUT_PORT_1) {
      continue;
    }
    {
      bus->regs[targetReg] = data[i];
      bus->lastWriteDataBytesReachedHardware++;
      bus->totalDataBytesReachedHardware++;
      bus->anyDataByteReachedHardware = true;
    }
  }

  return bus->lastWriteDataBytesReachedHardware;
}

void fillFakeReadData(FakeBus* bus, uint8_t startReg, uint8_t* rxData, size_t rxLen) {
  for (size_t i = 0; i < rxLen; ++i) {
    const uint8_t reg = fakePairedRegisterAt(startReg, i);
    rxData[i] = fakeReadableRegisterValue(bus, reg);
  }
}

Status fakeWrite(uint8_t addr, const uint8_t* data, size_t len, uint32_t timeoutMs,
                 void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->writeCalls++;
  bus->lastWriteDataBytesReachedHardware = 0;
  const size_t transactionIdx = beginFakeTransaction(
      bus, static_cast<uint8_t>('W'), addr, timeoutMs, data, len, 0, len);
  if (data == nullptr || len == 0) {
    return finishFakeTransaction(
        bus, transactionIdx, Status::Error(Err::INVALID_PARAM, "invalid fake write args"));
  }
  Status addressStatus = fakeCheckAddress(bus, addr);
  if (!addressStatus.ok()) {
    return finishFakeTransaction(bus, transactionIdx, addressStatus);
  }
  if (bus->shortWriteErrorRemaining > 0) {
    bus->shortWriteErrorRemaining--;
    const size_t accepted = (bus->shortWriteBytesBeforeError < len)
        ? bus->shortWriteBytesBeforeError
        : len;
    if (accepted > 0) {
      bus->commandPointer = data[0];
      (void)applyFakeWriteData(bus, data, accepted,
                               accepted > 0 ? accepted - 1U : 0U);
    }
    return finishFakeTransaction(bus, transactionIdx, bus->shortWriteError, nullptr, 0,
                                 bus->lastWriteDataBytesReachedHardware);
  }
  if (bus->partialWriteErrorRemaining > 0) {
    bus->partialWriteErrorRemaining--;
    bus->commandPointer = data[0];
    (void)applyFakeWriteData(bus, data, len, bus->partialWriteDataBytesBeforeError);
    return finishFakeTransaction(bus, transactionIdx, bus->partialWriteError, nullptr, 0,
                                 bus->lastWriteDataBytesReachedHardware);
  }
  if (bus->writeErrorRemaining > 0) {
    bus->writeErrorRemaining--;
    return finishFakeTransaction(bus, transactionIdx, bus->writeError);
  }
  if (bus->writeErrorOnCall != 0 && bus->writeCalls == bus->writeErrorOnCall) {
    bus->writeErrorOnCall = 0;
    return finishFakeTransaction(bus, transactionIdx, bus->writeError);
  }

  // Apply writes to register shadow
  bus->commandPointer = data[0];
  (void)applyFakeWriteData(bus, data, len, len > 0 ? len - 1 : 0);

  return finishFakeTransaction(bus, transactionIdx, Status::Ok(), nullptr, 0,
                               bus->lastWriteDataBytesReachedHardware);
}

Status fakeWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen, uint8_t* rxData,
                     size_t rxLen, uint32_t timeoutMs, void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->readCalls++;
  const size_t transactionIdx = beginFakeTransaction(
      bus, static_cast<uint8_t>('R'), addr, timeoutMs, txData, txLen, rxLen, rxLen);
  if (txData == nullptr || txLen == 0 || (rxLen > 0 && rxData == nullptr)) {
    return finishFakeTransaction(
        bus, transactionIdx, Status::Error(Err::INVALID_PARAM, "invalid fake write-read args"));
  }
  Status addressStatus = fakeCheckAddress(bus, addr);
  if (!addressStatus.ok()) {
    return finishFakeTransaction(bus, transactionIdx, addressStatus);
  }
  bus->commandPointer = txData[0];
  if (bus->shortReadErrorRemaining > 0) {
    bus->shortReadErrorRemaining--;
    const size_t produced = (bus->shortReadBytesBeforeError < rxLen)
        ? bus->shortReadBytesBeforeError
        : rxLen;
    fillFakeReadData(bus, txData[0], rxData, produced);
    return finishFakeTransaction(bus, transactionIdx, bus->shortReadError, rxData, produced);
  }
  if (bus->readErrorRemaining > 0) {
    bus->readErrorRemaining--;
    return finishFakeTransaction(bus, transactionIdx, bus->readError);
  }

  const uint8_t reg = txData[0];
  fillFakeReadData(bus, reg, rxData, rxLen);
  clearFakeInterruptsForRead(bus, reg, rxLen);
  if (reg == cmd::REG_INPUT_PORT_0 || reg == cmd::REG_INPUT_PORT_1) {
    attemptFakeInterleaver(bus);
  }

  return finishFakeTransaction(bus, transactionIdx, Status::Ok(), rxData, rxLen);
}

Status fakeLock(void* user, uint32_t) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->lockCalls++;
  recordFakeEvent(bus, static_cast<uint8_t>('L'), 0, 0);
  if (bus->lockErrorRemaining > 0) {
    bus->lockErrorRemaining--;
    return bus->lockError;
  }
  bus->lockHeld = true;
  return Status::Ok();
}

void fakeUnlock(void* user) {
  FakeBus* bus = static_cast<FakeBus*>(user);
  bus->unlockCalls++;
  bus->lockHeld = false;
  recordFakeEvent(bus, static_cast<uint8_t>('U'), 0, 0);
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

void setFakeAddress(FakeBus& bus, Config& cfg, uint8_t address) {
  bus.deviceAddress = address;
  bus.enforceAddress = true;
  cfg.i2cAddress = address;
}

void resetFakeWriteReach(FakeBus& bus) {
  bus.anyDataByteReachedHardware = false;
  bus.lastWriteDataBytesReachedHardware = 0;
  bus.totalDataBytesReachedHardware = 0;
}

void injectPartialWriteFailure(FakeBus& bus, const Status& st, size_t dataBytesBeforeError) {
  bus.partialWriteErrorRemaining = 1;
  bus.partialWriteError = st;
  bus.partialWriteDataBytesBeforeError = dataBytesBeforeError;
  resetFakeWriteReach(bus);
}

void injectShortWriteFailure(FakeBus& bus, const Status& st, size_t acceptedWireBytes) {
  bus.shortWriteErrorRemaining = 1;
  bus.shortWriteError = st;
  bus.shortWriteBytesBeforeError = acceptedWireBytes;
  resetFakeWriteReach(bus);
}

void injectShortReadFailure(FakeBus& bus, const Status& st, size_t producedBytes) {
  bus.shortReadErrorRemaining = 1;
  bus.shortReadError = st;
  bus.shortReadBytesBeforeError = producedBytes;
}

void injectUnavailableReadFailure(FakeBus& bus, size_t producedBytes) {
  injectShortReadFailure(
      bus, Status::Error(Err::I2C_ERROR, "forced unavailable read data", -60), producedBytes);
}

void fakePowerCycle(FakeBus& bus) {
  bus.regs[cmd::REG_INPUT_PORT_0] = 0xFF;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0xFF;
  bus.regs[cmd::REG_OUTPUT_PORT_0] = cmd::DEFAULT_OUTPUT;
  bus.regs[cmd::REG_OUTPUT_PORT_1] = cmd::DEFAULT_OUTPUT;
  bus.regs[cmd::REG_POLARITY_INV_0] = cmd::DEFAULT_POLARITY;
  bus.regs[cmd::REG_POLARITY_INV_1] = cmd::DEFAULT_POLARITY;
  bus.regs[cmd::REG_CONFIG_PORT_0] = cmd::DEFAULT_CONFIG;
  bus.regs[cmd::REG_CONFIG_PORT_1] = cmd::DEFAULT_CONFIG;
  bus.commandPointer = 0;
  bus.intPending0 = false;
  bus.intPending1 = false;
}

void fakeSetInputs(FakeBus& bus, const PortData& data) {
  bus.regs[cmd::REG_INPUT_PORT_0] = data.port0;
  bus.regs[cmd::REG_INPUT_PORT_1] = data.port1;
}

void fakeDrivePin(FakeBus& bus, Pin pin, bool high) {
  const uint8_t reg = (pin < cmd::PINS_PER_PORT)
      ? cmd::REG_INPUT_PORT_0
      : cmd::REG_INPUT_PORT_1;
  const uint8_t mask = static_cast<uint8_t>(1U << (pin % cmd::PINS_PER_PORT));
  if (high) {
    bus.regs[reg] = static_cast<uint8_t>(bus.regs[reg] | mask);
  } else {
    bus.regs[reg] = static_cast<uint8_t>(bus.regs[reg] & ~mask);
  }
}

void fakeMutateOutputLatch(FakeBus& bus, const PortData& data) {
  bus.regs[cmd::REG_OUTPUT_PORT_0] = data.port0;
  bus.regs[cmd::REG_OUTPUT_PORT_1] = data.port1;
}

void fakeMutateConfiguration(FakeBus& bus, const PortData& data) {
  bus.regs[cmd::REG_CONFIG_PORT_0] = data.port0;
  bus.regs[cmd::REG_CONFIG_PORT_1] = data.port1;
}

void fakeMutatePolarity(FakeBus& bus, const PortData& data) {
  bus.regs[cmd::REG_POLARITY_INV_0] = data.port0;
  bus.regs[cmd::REG_POLARITY_INV_1] = data.port1;
}

void resetFakeTransactionLog(FakeBus& bus) {
  bus.transactionCount = 0;
}

void resetFakeLockStats(FakeBus& bus) {
  bus.lockCalls = 0;
  bus.unlockCalls = 0;
  bus.lockHeld = false;
  bus.interleaverAttempts = 0;
  bus.interleaverRan = 0;
  bus.interleaverBlocked = 0;
}

void enableFakeLock(Config& cfg, FakeBus& bus) {
  cfg.i2cLock = fakeLock;
  cfg.i2cUnlock = fakeUnlock;
  cfg.lockUser = &bus;
}

void assertFakeEvent(const FakeBus& bus, size_t idx, uint8_t type, uint8_t reg,
                     size_t len) {
  TEST_ASSERT_TRUE(idx < bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT8(type, bus.transactionType[idx]);
  TEST_ASSERT_EQUAL_HEX8(reg, bus.transactionReg[idx]);
  TEST_ASSERT_EQUAL_UINT(len, bus.transactionLen[idx]);
}

void assertErrataWriteAt(const FakeBus& bus, size_t idx) {
  assertFakeEvent(bus, idx, static_cast<uint8_t>('W'), cmd::ERRATA_SAFE_CMD, 1);
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.commandPointer);
}

void assertBusyNoI2c(const FakeBus& bus, const Status& st,
                     uint32_t readsBefore, uint32_t writesBefore) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
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
  TEST_ASSERT_NULL(cfg.i2cLock);
  TEST_ASSERT_NULL(cfg.i2cUnlock);
  TEST_ASSERT_NULL(cfg.lockUser);
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

void test_begin_accepts_all_pca9555_address_pins_and_logs_callback_address() {
  for (uint8_t address = cmd::BASE_ADDRESS; address <= cmd::MAX_ADDRESS; ++address) {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    Config cfg = makeConfig(bus);
    setFakeAddress(bus, cfg, address);

    Status st = dev.begin(cfg);
    TEST_ASSERT_TRUE(st.ok());
    TEST_ASSERT_TRUE(bus.transactionCount > 0u);
    for (size_t i = 0; i < bus.transactionCount; ++i) {
      if (bus.transactionType[i] == static_cast<uint8_t>('W') ||
          bus.transactionType[i] == static_cast<uint8_t>('R')) {
        TEST_ASSERT_EQUAL_HEX8(address, bus.transactions[i].address);
        TEST_ASSERT_EQUAL_UINT32(cfg.i2cTimeoutMs, bus.transactions[i].timeoutMs);
        TEST_ASSERT_TRUE(bus.transactions[i].status.ok());
      }
    }
  }
}

void test_begin_rejects_address_matrix_without_touching_bus() {
  const uint8_t invalidAddresses[] = {0x00, 0x1F, 0x28, 0xFF};
  for (size_t i = 0; i < sizeof(invalidAddresses); ++i) {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    Config cfg = makeConfig(bus);
    cfg.i2cAddress = invalidAddresses[i];

    Status st = dev.begin(cfg);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_UINT32(0u, bus.readCalls);
    TEST_ASSERT_EQUAL_UINT32(0u, bus.writeCalls);
    TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
  }
}

void test_begin_reports_device_absent_or_wrong_address_as_not_found() {
  {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    Config cfg = makeConfig(bus);
    bus.devicePresent = false;

    Status st = dev.begin(cfg);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(bus.addressError.detail, st.detail);
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                            static_cast<uint8_t>(bus.transactions[0].status.code));
    TEST_ASSERT_EQUAL_HEX8(0x00, bus.transactions[0].pointerAfter);
  }

  {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    Config cfg = makeConfig(bus);
    bus.enforceAddress = true;
    bus.deviceAddress = static_cast<uint8_t>(cfg.i2cAddress + 1U);

    Status st = dev.begin(cfg);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::DEVICE_NOT_FOUND),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(bus.addressError.detail, st.detail);
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
    TEST_ASSERT_EQUAL_HEX8(cfg.i2cAddress, bus.transactions[0].address);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_ADDR),
                            static_cast<uint8_t>(bus.transactions[0].status.code));
  }
}

void test_begin_presence_read_preserves_non_address_transport_errors() {
  const Status errors[] = {
    Status::Error(Err::I2C_NACK_DATA, "begin nack data", -81),
    Status::Error(Err::I2C_TIMEOUT, "begin timeout", -82),
    Status::Error(Err::I2C_BUS, "begin bus", -83),
    Status::Error(Err::I2C_ERROR, "begin generic", -84)
  };

  for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    Config cfg = makeConfig(bus);
    bus.readErrorRemaining = 1;
    bus.readError = errors[i];

    Status st = dev.begin(cfg);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(errors[i].detail, st.detail);
    TEST_ASSERT_FALSE(dev.isInitialized());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                            static_cast<uint8_t>(dev.state()));
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(bus.transactions[0].status.code));
  }
}

void test_begin_rejects_zero_timeout() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cTimeoutMs = 0;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG), static_cast<uint8_t>(st.code));
}

void test_begin_rejects_partial_lock_hooks() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cLock = fakeLock;
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));

  cfg = makeConfig(bus);
  cfg.i2cUnlock = fakeUnlock;
  st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
}

void test_failed_begin_clears_stale_runtime_snapshot() {
  FakeBus bus;
  PCA9555::PCA9555 dev;

  Config good = makeConfig(bus);
  good.i2cAddress = 0x27;
  good.outputPort0 = 0xAA;
  good.configPort0 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(good).ok());
  TEST_ASSERT_TRUE(dev.writeOutput(Port::PORT_0, 0x55).ok());

  Config bad = makeConfig(bus);
  bad.i2cWrite = nullptr;
  bad.i2cWriteRead = nullptr;
  Status st = dev.begin(bad);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));

  const SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_EQUAL_HEX8(cmd::BASE_ADDRESS, snap.config.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, snap.config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, snap.config.offlineThreshold);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, snap.config.outputPort0);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, snap.config.outputPort1);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, snap.config.configPort0);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, snap.config.configPort1);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.totalFailures);
  TEST_ASSERT_EQUAL_UINT8(0u, snap.consecutiveFailures);
}

void test_failed_begin_apply_clears_runtime_snapshot() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.i2cAddress = 0x27;
  cfg.outputPort0 = 0xAA;
  cfg.configPort0 = 0x00;
  bus.writeErrorRemaining = 1;

  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));

  const SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_FALSE(snap.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(snap.state));
  TEST_ASSERT_EQUAL_HEX8(cmd::BASE_ADDRESS, snap.config.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(50u, snap.config.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, snap.config.offlineThreshold);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, snap.config.outputPort0);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, snap.config.outputPort1);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, snap.config.configPort0);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, snap.config.configPort1);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.totalSuccess);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.totalFailures);
  TEST_ASSERT_EQUAL_UINT8(0u, snap.consecutiveFailures);
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
  SettingsSnapshot statusSnapshot;
  TEST_ASSERT_TRUE(dev.getSettings(statusSnapshot).ok());
  TEST_ASSERT_TRUE(snapshot.initialized);
  TEST_ASSERT_TRUE(statusSnapshot.initialized);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(snapshot.state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(snapshot.state),
                          static_cast<uint8_t>(statusSnapshot.state));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(dev.state()),
                          static_cast<uint8_t>(dev.driverState()));
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

void test_begin_checks_both_configuration_defaults_not_input_identity() {
  {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    fakeSetInputs(bus, PortData{0x12, 0x34});

    Status st = dev.begin(makeConfig(bus));
    TEST_ASSERT_TRUE(st.ok());

    PortData inputs;
    TEST_ASSERT_TRUE(dev.readInputs(inputs).ok());
    TEST_ASSERT_EQUAL_HEX8(0x12, inputs.port0);
    TEST_ASSERT_EQUAL_HEX8(0x34, inputs.port1);
  }

  {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    bus.regs[cmd::REG_CONFIG_PORT_1] = 0x7F;

    Status st = dev.begin(makeConfig(bus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::CONFIG_REG_MISMATCH),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(0x7FFF, st.detail);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                            static_cast<uint8_t>(dev.state()));
  }
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

void test_begin_ordering_remains_safe() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0xAA;
  cfg.outputPort1 = 0x55;
  cfg.polarityPort0 = 0x11;
  cfg.polarityPort1 = 0x22;
  cfg.configPort0 = 0x0F;
  cfg.configPort1 = 0xF0;

  Status st = dev.begin(cfg);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(bus.transactionCount >= 6u);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('R'), bus.transactionType[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.transactionReg[0]);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionLen[0]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('W'), bus.transactionType[1]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.transactionReg[1]);
  TEST_ASSERT_EQUAL_HEX8(0xAA, bus.transactionData0[1]);
  TEST_ASSERT_EQUAL_HEX8(0x55, bus.transactionData1[1]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('W'), bus.transactionType[2]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_POLARITY_INV_0, bus.transactionReg[2]);
  TEST_ASSERT_EQUAL_HEX8(0x11, bus.transactionData0[2]);
  TEST_ASSERT_EQUAL_HEX8(0x22, bus.transactionData1[2]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('W'), bus.transactionType[3]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.transactionReg[3]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.transactionData0[3]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.transactionData1[3]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('R'), bus.transactionType[4]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_INPUT_PORT_0, bus.transactionReg[4]);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionLen[4]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('W'), bus.transactionType[5]);
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.transactionReg[5]);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionLen[5]);
}

// ===========================================================================
// Health timestamps
// ===========================================================================

void test_null_now_ms_keeps_health_timestamps_zero() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.nowMs = nullptr;
  cfg.timeUser = nullptr;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  setMillis(4321);
  Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.lastErrorMs());
  TEST_ASSERT_GREATER_THAN_UINT32(0u, dev.totalSuccess());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced no-clock error", -6);
  setMillis(8765);
  st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0u, dev.lastErrorMs());
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
}

void test_now_ms_callback_updates_health_timestamps() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.nowMs = 2222;
  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_ERROR, "forced clocked error", -5);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(2222u, dev.lastErrorMs());

  bus.nowMs = 3333;
  st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(3333u, dev.lastOkMs());
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
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(beforeSuccess, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(beforeFailures, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(beforeState),
                          static_cast<uint8_t>(dev.state()));
}

void test_probe_error_matrix_preserves_transport_error_kind() {
  const Status errors[] = {
    Status::Error(Err::I2C_NACK_ADDR, "probe nack addr", -80),
    Status::Error(Err::I2C_NACK_DATA, "probe nack data", -81),
    Status::Error(Err::I2C_TIMEOUT, "probe timeout", -82),
    Status::Error(Err::I2C_BUS, "probe bus", -83),
    Status::Error(Err::I2C_ERROR, "probe generic", -84)
  };

  for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    const uint32_t beforeSuccess = dev.totalSuccess();
    const uint32_t beforeFailures = dev.totalFailures();
    resetFakeTransactionLog(bus);
    bus.readErrorRemaining = 1;
    bus.readError = errors[i];

    Status st = dev.probe();
    const Err expected = (errors[i].code == Err::I2C_NACK_ADDR)
        ? Err::DEVICE_NOT_FOUND
        : errors[i].code;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(errors[i].detail, st.detail);
    TEST_ASSERT_EQUAL_UINT32(beforeSuccess, dev.totalSuccess());
    TEST_ASSERT_EQUAL_UINT32(beforeFailures, dev.totalFailures());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                            static_cast<uint8_t>(dev.state()));
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(bus.transactions[0].status.code));
  }
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

void test_offline_latches_normal_read_without_i2c_until_recover() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -11);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t readsBefore = bus.readCalls;
  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  TEST_ASSERT_TRUE(dev.recover().ok());
  TEST_ASSERT_GREATER_THAN_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
}

void test_probe_blocks_offline_without_i2c() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -12);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t readsBefore = bus.readCalls;
  resetFakeTransactionLog(bus);
  Status st = dev.probe();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
}

void test_offline_input_read_checks_latch_before_lock() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -16);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t readsBefore = bus.readCalls;
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);
  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
}

void test_failed_recover_from_offline_preserves_latch_after_partial_success() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 3;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 3;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -12);
  PortData data;
  for (uint8_t i = 0; i < 3; ++i) {
    Status st = dev.readInputs(data);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                            static_cast<uint8_t>(st.code));
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(3u, dev.consecutiveFailures());

  bus.writeErrorRemaining = 1;
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.consecutiveFailures() >= 3u);

  const uint32_t readsBefore = bus.readCalls;
  st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
}

void test_failed_recover_from_offline_preserves_latch_on_in_progress() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -14);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::IN_PROGRESS, "forced in progress", -15);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::IN_PROGRESS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.consecutiveFailures() >= 1u);

  const uint32_t readsBefore = bus.readCalls;
  resetFakeTransactionLog(bus);
  PortData data;
  st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
}

// ===========================================================================
// Hardware dirty-state diagnostics
// ===========================================================================

void test_failed_validation_does_not_mark_hardware_dirty() {
  FakeBus bus;
  PCA9555::PCA9555 dev;

  Status st = dev.writeRegister(cmd::REG_OUTPUT_PORT_0, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.hardwareStateDirty());

  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  st = dev.writeRegister(cmd::REG_INPUT_PORT_0, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_failed_read_does_not_mark_hardware_dirty() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_TIMEOUT, "forced read timeout", -20);
  uint8_t value = 0;
  Status st = dev.readOutput(Port::PORT_0, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.hardwareStateDirty());
}

void test_fail_before_apply_write_marks_dirty_without_cache_update() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeWriteReach(bus);

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced pre-apply timeout", -21);
  Status st = dev.writeOutput(Port::PORT_0, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(bus.anyDataByteReachedHardware);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort0);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_partial_output_pair_write_marks_dirty_and_preserves_error() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_NACK_DATA, "forced output partial", -22), 1);
  Status st = dev.writeOutputs(PortData{0xAA, 0x55});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-22, st.detail);
  TEST_ASSERT_TRUE(bus.anyDataByteReachedHardware);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.totalDataBytesReachedHardware);
  TEST_ASSERT_EQUAL_HEX8(0xAA, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort0);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort1);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_partial_configuration_pair_write_marks_dirty_and_preserves_error() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.configPort0 = 0x00;
  cfg.configPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_TIMEOUT, "forced config partial", -23), 1);
  Status st = dev.setConfiguration(PortData{0x0F, 0xF0});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-23, st.detail);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.regs[cmd::REG_CONFIG_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, dev.getSettings().config.configPort0);
  TEST_ASSERT_EQUAL_HEX8(0x00, dev.getSettings().config.configPort1);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_partial_polarity_pair_write_marks_dirty_and_preserves_error() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_BUS, "forced polarity partial", -24), 1);
  Status st = dev.setPolarity(PortData{0x11, 0x22});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-24, st.detail);
  TEST_ASSERT_EQUAL_HEX8(0x11, bus.regs[cmd::REG_POLARITY_INV_0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_POLARITY, bus.regs[cmd::REG_POLARITY_INV_1]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_POLARITY, dev.getSettings().config.polarityPort0);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_POLARITY, dev.getSettings().config.polarityPort1);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_direct_register_write_failure_marks_dirty() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_NACK_DATA, "forced direct partial", -25), 1);
  Status st = dev.writeRegister(cmd::REG_OUTPUT_PORT_0, 0x12);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort0);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_direct_odd_start_pair_write_failure_marks_dirty() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint8_t values[2] = {0x12, 0x34};
  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_BUS, "forced odd direct partial", -31), 1);
  Status st = dev.writeRegisters(cmd::REG_OUTPUT_PORT_1, values, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort0);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort1);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_hardware_dirty_status_appears_in_settings_snapshot() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_BUS, "forced snapshot dirty", -26), 1);
  (void)dev.writeOutputs(PortData{0x00, 0x00});
  SettingsSnapshot snap = dev.getSettings();
  TEST_ASSERT_TRUE(snap.hardwareStateDirty);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(snap.hardwareStateDirtyError.code));
  TEST_ASSERT_EQUAL_INT32(-26, snap.hardwareStateDirtyError.detail);
}

void test_hardware_dirty_survives_unrelated_successful_reads() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_NACK_DATA, "forced dirty before read", -27), 1);
  (void)dev.writeOutputs(PortData{0xAA, 0x55});
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());

  PortData inputs;
  Status st = dev.readInputs(inputs);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_hardware_dirty_clears_after_full_successful_recover() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_NACK_DATA, "forced dirty before recover", -28), 1);
  (void)dev.writeOutputs(PortData{0xAA, 0x55});
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());

  Status st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::OK),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_hardware_dirty_does_not_clear_after_partial_recover() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_NACK_DATA, "forced dirty before failed recover", -29), 1);
  (void)dev.writeOutputs(PortData{0xAA, 0x55});
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_BUS, "forced recover partial", -30), 1);
  Status st = dev.recover();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_begin_validation_failure_preserves_existing_hardware_dirty() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_NACK_DATA, "forced dirty before bad begin", -32), 1);
  (void)dev.writeOutputs(PortData{0xAA, 0x55});
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());

  Config badConfig;
  Status st = dev.begin(badConfig);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_CONFIG),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
  TEST_ASSERT_EQUAL_INT32(-32, dev.hardwareStateDirtyError().detail);
}

void test_failed_begin_apply_partial_write_marks_dirty_and_uninitialized() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.outputPort0 = 0xA0;
  cfg.outputPort1 = 0x5A;

  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_BUS, "forced begin apply partial", -33), 1);
  Status st = dev.begin(cfg);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-33, st.detail);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_HEX8(0xA0, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_transport_read_error_matrix_updates_health_without_dirty_state() {
  const Status errors[] = {
    Status::Error(Err::I2C_NACK_ADDR, "matrix read nack addr", -50),
    Status::Error(Err::I2C_NACK_DATA, "matrix read nack data", -51),
    Status::Error(Err::I2C_TIMEOUT, "matrix read timeout", -52),
    Status::Error(Err::I2C_BUS, "matrix read bus", -53),
    Status::Error(Err::I2C_ERROR, "matrix read generic", -54)
  };

  for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    resetFakeTransactionLog(bus);
    bus.readErrorRemaining = 1;
    bus.readError = errors[i];
    uint8_t value = 0xEE;
    Status st = dev.readOutput(Port::PORT_0, value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(errors[i].detail, st.detail);
    TEST_ASSERT_EQUAL_HEX8(0xEE, value);
    TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
    TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                            static_cast<uint8_t>(dev.state()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(dev.lastError().code));
    TEST_ASSERT_FALSE(dev.hardwareStateDirty());
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(bus.transactions[0].status.code));
  }
}

void test_transport_write_error_matrix_updates_health_and_marks_dirty() {
  const Status errors[] = {
    Status::Error(Err::I2C_NACK_ADDR, "matrix write nack addr", -55),
    Status::Error(Err::I2C_NACK_DATA, "matrix write nack data", -56),
    Status::Error(Err::I2C_TIMEOUT, "matrix write timeout", -57),
    Status::Error(Err::I2C_BUS, "matrix write bus", -58),
    Status::Error(Err::I2C_ERROR, "matrix write generic", -59)
  };

  for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    resetFakeTransactionLog(bus);
    resetFakeWriteReach(bus);
    bus.writeErrorRemaining = 1;
    bus.writeError = errors[i];
    Status st = dev.writeOutput(Port::PORT_0, 0x00);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(errors[i].detail, st.detail);
    TEST_ASSERT_FALSE(bus.anyDataByteReachedHardware);
    TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_0]);
    TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
    TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                            static_cast<uint8_t>(dev.state()));
    TEST_ASSERT_TRUE(dev.hardwareStateDirty());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(errors[i].code),
                            static_cast<uint8_t>(bus.transactions[0].status.code));
  }
}

void test_partial_pair_write_all_bytes_then_error_marks_dirty_without_cache_sync() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  resetFakeTransactionLog(bus);
  injectPartialWriteFailure(
      bus, Status::Error(Err::I2C_NACK_DATA, "forced late pair nack", -61), 2);
  Status st = dev.writeOutputs(PortData{0x12, 0x34});
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort0);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort1);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactions[0].dataBytesReachedHardware);
}

void test_short_write_failures_record_command_boundary() {
  {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
    const uint8_t pointerBefore = bus.commandPointer;

    resetFakeTransactionLog(bus);
    injectShortWriteFailure(
        bus, Status::Error(Err::I2C_NACK_DATA, "forced short before command", -62), 0);
    Status st = dev.writeOutput(Port::PORT_0, 0x11);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_HEX8(pointerBefore, bus.commandPointer);
    TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_0]);
    TEST_ASSERT_EQUAL_UINT(0u, bus.transactions[0].dataBytesReachedHardware);
  }

  {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    resetFakeTransactionLog(bus);
    injectShortWriteFailure(
        bus, Status::Error(Err::I2C_NACK_DATA, "forced short after command", -63), 1);
    Status st = dev.writeOutput(Port::PORT_0, 0x22);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.commandPointer);
    TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_0]);
    TEST_ASSERT_EQUAL_UINT(0u, bus.transactions[0].dataBytesReachedHardware);
  }

  {
    FakeBus bus;
    PCA9555::PCA9555 dev;
    TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

    resetFakeTransactionLog(bus);
    injectShortWriteFailure(
        bus, Status::Error(Err::I2C_NACK_DATA, "forced short after data", -64), 2);
    Status st = dev.writeOutputs(PortData{0x33, 0x44});
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_HEX8(0x33, bus.regs[cmd::REG_OUTPUT_PORT_0]);
    TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.regs[cmd::REG_OUTPUT_PORT_1]);
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactions[0].dataBytesReachedHardware);
  }
}

void test_short_read_and_unavailable_data_do_not_sync_cache_on_failure() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  fakeMutateOutputLatch(bus, PortData{0x12, 0x34});

  resetFakeTransactionLog(bus);
  uint8_t readback[2] = {0xAA, 0xBB};
  injectShortReadFailure(
      bus, Status::Error(Err::I2C_TIMEOUT, "forced short read timeout", -65), 1);
  Status st = dev.readRegisters(cmd::REG_OUTPUT_PORT_0, readback, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(0x12, readback[0]);
  TEST_ASSERT_EQUAL_HEX8(0xBB, readback[1]);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactions[0].rxLen);
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.transactions[0].rx[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, dev.getSettings().config.outputPort0);
  TEST_ASSERT_FALSE(dev.hardwareStateDirty());

  resetFakeTransactionLog(bus);
  uint8_t value = 0xEE;
  injectUnavailableReadFailure(bus, 0);
  st = dev.readOutput(Port::PORT_0, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-60, st.detail);
  TEST_ASSERT_EQUAL_HEX8(0xEE, value);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactions[0].rxLen);
  TEST_ASSERT_FALSE(dev.hardwareStateDirty());
}

void test_failure_threshold_enters_offline_and_blocks_bus_until_recover() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 3;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  uint8_t value = 0;
  for (uint8_t i = 1; i <= 3; ++i) {
    bus.readErrorRemaining = 1;
    bus.readError = Status::Error(Err::I2C_TIMEOUT, "threshold timeout", -70 - i);
    Status st = dev.readOutput(Port::PORT_0, value);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                            static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_UINT8(i, dev.consecutiveFailures());
    TEST_ASSERT_EQUAL_UINT32(i, dev.totalFailures());
    if (i < 3) {
      TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                              static_cast<uint8_t>(dev.state()));
    }
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  resetFakeTransactionLog(bus);
  Status st = dev.readOutput(Port::PORT_0, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT32(3u, dev.totalFailures());

  st = dev.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
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

void test_example_transport_write_read_maps_wire_errors_and_short_read() {
  const uint8_t tx = 0x06;
  uint8_t rx[2] = {};
  const uint8_t wireResults[] = {2, 3, 4, 5};
  const Err expected[] = {
    Err::I2C_NACK_ADDR,
    Err::I2C_NACK_DATA,
    Err::I2C_BUS,
    Err::I2C_TIMEOUT
  };

  for (size_t i = 0; i < sizeof(wireResults); ++i) {
    Wire._clearRequestFromOverride();
    Wire._setEndTransmissionResult(wireResults[i]);
    Status st = transport::wireWriteRead(0x20, &tx, 1, rx, sizeof(rx), 50, &Wire);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected[i]), static_cast<uint8_t>(st.code));
    TEST_ASSERT_EQUAL_INT32(wireResults[i], st.detail);
  }

  Wire._clearEndTransmissionResult();
  Wire._setRequestFromResult(1);
  Status st = transport::wireWriteRead(0x20, &tx, 1, rx, sizeof(rx), 50, &Wire);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(1, st.detail);

  Wire._clearRequestFromOverride();
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

  resetFakeTransactionLog(bus);

  PortData data;
  TEST_ASSERT_TRUE(dev.readInputs(data).ok());

  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertErrataWriteAt(bus, 1);
}

void test_read_inputs_skips_errata_workaround_when_disabled() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  resetFakeTransactionLog(bus);

  PortData data;
  TEST_ASSERT_TRUE(dev.readInputs(data).ok());

  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
}

void test_read_register_input_port_applies_errata_workaround() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  resetFakeTransactionLog(bus);
  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readRegister(cmd::REG_INPUT_PORT_0, value).ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 1);
  assertErrataWriteAt(bus, 1);
}

void test_all_input_read_paths_write_exact_errata_command() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  PortData data;
  resetFakeTransactionLog(bus);
  TEST_ASSERT_TRUE(dev.readInputs(data).ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertErrataWriteAt(bus, 1);

  uint8_t value = 0;
  resetFakeTransactionLog(bus);
  TEST_ASSERT_TRUE(dev.readInput(Port::PORT_0, value).ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 1);
  assertErrataWriteAt(bus, 1);

  resetFakeTransactionLog(bus);
  TEST_ASSERT_TRUE(dev.readInput(Port::PORT_1, value).ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_1, 1);
  assertErrataWriteAt(bus, 1);

  bool state = false;
  resetFakeTransactionLog(bus);
  TEST_ASSERT_TRUE(dev.readPin(0, state).ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 1);
  assertErrataWriteAt(bus, 1);

  resetFakeTransactionLog(bus);
  TEST_ASSERT_TRUE(dev.readPin(8, state).ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_1, 1);
  assertErrataWriteAt(bus, 1);

  resetFakeTransactionLog(bus);
  TEST_ASSERT_TRUE(dev.readRegister(cmd::REG_INPUT_PORT_1, value).ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_1, 1);
  assertErrataWriteAt(bus, 1);

  uint8_t inputRegs[2] = {};
  resetFakeTransactionLog(bus);
  TEST_ASSERT_TRUE(dev.readRegisters(cmd::REG_INPUT_PORT_1, inputRegs, 2).ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_1, 2);
  assertErrataWriteAt(bus, 1);
}

void test_read_inputs_errata_write_failure_is_reported_and_updates_health() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0xAB;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0xCD;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  resetFakeTransactionLog(bus);
  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced errata nack", -40);
  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-40, st.detail);
  TEST_ASSERT_EQUAL_HEX8(0xAB, data.port0);
  TEST_ASSERT_EQUAL_HEX8(0xCD, data.port1);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('W'), cmd::ERRATA_SAFE_CMD, 1);
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(1u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.lastError().code));
  TEST_ASSERT_FALSE(dev.hardwareStateDirty());
}

void test_read_inputs_read_failure_does_not_pointer_park() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  resetFakeTransactionLog(bus);
  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_TIMEOUT, "forced read timeout", -41);
  PortData data{0x12, 0x34};
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-41, st.detail);
  TEST_ASSERT_EQUAL_HEX8(0x12, data.port0);
  TEST_ASSERT_EQUAL_HEX8(0x34, data.port1);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(1u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(dev.state()));
}

void test_read_inputs_and_clear_interrupt_returns_both_ports() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0xA5;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0x5A;
  bus.intPending0 = true;
  bus.intPending1 = true;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  bus.intPending0 = true;
  bus.intPending1 = true;
  resetFakeTransactionLog(bus);

  uint16_t value = 0;
  Status st = dev.readInputsAndClearInterrupt(value);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX16(0x5AA5, value);
  TEST_ASSERT_FALSE(bus.intPending0);
  TEST_ASSERT_FALSE(bus.intPending1);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertErrataWriteAt(bus, 1);
}

void test_clear_interrupts_reads_both_ports_then_parks_pointer() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  bus.intPending0 = true;
  bus.intPending1 = true;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  bus.intPending0 = true;
  bus.intPending1 = true;
  resetFakeTransactionLog(bus);

  Status st = dev.clearInterrupts();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(bus.intPending0);
  TEST_ASSERT_FALSE(bus.intPending1);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertErrataWriteAt(bus, 1);
}

void test_clear_interrupts_errata_disabled_reads_only() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  bus.intPending0 = true;
  bus.intPending1 = true;
  resetFakeTransactionLog(bus);

  Status st = dev.clearInterrupts();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(bus.intPending0);
  TEST_ASSERT_FALSE(bus.intPending1);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
}

void test_read_input_port0_clears_only_port0_interrupt() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  bus.intPending0 = true;
  bus.intPending1 = true;
  resetFakeTransactionLog(bus);

  uint8_t value = 0;
  Status st = dev.readInput(Port::PORT_0, value);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(bus.intPending0);
  TEST_ASSERT_TRUE(bus.intPending1);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 1);
  assertErrataWriteAt(bus, 1);
}

void test_read_input_port1_clears_only_port1_interrupt() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  bus.intPending0 = true;
  bus.intPending1 = true;
  resetFakeTransactionLog(bus);

  uint8_t value = 0;
  Status st = dev.readInput(Port::PORT_1, value);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(bus.intPending0);
  TEST_ASSERT_FALSE(bus.intPending1);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_1, 1);
  assertErrataWriteAt(bus, 1);
}

void test_apply_interrupt_errata_workaround_parks_pointer() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  Status st = dev.applyInterruptErrataWorkaround();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertErrataWriteAt(bus, 0);
}

void test_apply_interrupt_errata_workaround_locked_variant_uses_hooks() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);

  Status st = dev.applyInterruptErrataWorkaround();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.unlockCalls);
  TEST_ASSERT_FALSE(bus.lockHeld);
  TEST_ASSERT_EQUAL_UINT(3u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('L'), 0, 0);
  assertErrataWriteAt(bus, 1);
  assertFakeEvent(bus, 2, static_cast<uint8_t>('U'), 0, 0);
}

void test_apply_interrupt_errata_workaround_unlocked_variant_skips_hooks() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);

  Status st = dev.applyInterruptErrataWorkaroundUnlocked();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertErrataWriteAt(bus, 0);
}

void test_input_read_errata_lock_wraps_full_sequence() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);

  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.unlockCalls);
  TEST_ASSERT_FALSE(bus.lockHeld);
  TEST_ASSERT_EQUAL_UINT(4u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('L'), 0, 0);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertFakeEvent(bus, 2, static_cast<uint8_t>('W'), cmd::ERRATA_SAFE_CMD, 1);
  assertFakeEvent(bus, 3, static_cast<uint8_t>('U'), 0, 0);
}

void test_input_read_errata_lock_releases_on_read_failure() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);
  bus.readErrorRemaining = 1;

  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(1u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.unlockCalls);
  TEST_ASSERT_FALSE(bus.lockHeld);
  TEST_ASSERT_EQUAL_UINT(3u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('L'), 0, 0);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertFakeEvent(bus, 2, static_cast<uint8_t>('U'), 0, 0);
}

void test_input_read_errata_lock_releases_on_errata_failure() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);
  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_BUS, "forced errata bus", -42);

  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-42, st.detail);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.unlockCalls);
  TEST_ASSERT_FALSE(bus.lockHeld);
  TEST_ASSERT_EQUAL_UINT(4u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('L'), 0, 0);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertFakeEvent(bus, 2, static_cast<uint8_t>('W'), cmd::ERRATA_SAFE_CMD, 1);
  assertFakeEvent(bus, 3, static_cast<uint8_t>('U'), 0, 0);
}

void test_input_read_lock_failure_skips_i2c_and_unlock() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);
  bus.lockErrorRemaining = 1;
  bus.lockError = Status::Error(Err::BUSY, "forced lock busy", -43);

  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_INT32(-43, st.detail);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_FALSE(bus.lockHeld);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('L'), 0, 0);
}

void test_input_read_validation_failure_does_not_lock() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);

  uint8_t value = 0;
  const Port invalidPort = static_cast<Port>(2);
  Status st = dev.readInput(invalidPort, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_FALSE(bus.lockHeld);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
}

void test_input_read_errata_lock_blocks_interleaved_external_read() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);
  bus.interleaverEnabled = true;

  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(1u, bus.interleaverAttempts);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.interleaverRan);
  TEST_ASSERT_EQUAL_UINT32(1u, bus.interleaverBlocked);
  TEST_ASSERT_EQUAL_UINT(4u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('L'), 0, 0);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertFakeEvent(bus, 2, static_cast<uint8_t>('W'), cmd::ERRATA_SAFE_CMD, 1);
  assertFakeEvent(bus, 3, static_cast<uint8_t>('U'), 0, 0);
}

void test_read_inputs_job_without_errata_budget_1_completes_one_read() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  bus.regs[cmd::REG_INPUT_PORT_0] = 0x12;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0x34;
  resetFakeTransactionLog(bus);

  Status st = dev.startReadInputsJob();
  TEST_ASSERT_TRUE(st.inProgress());
  st = dev.pollJob(2000, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);

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
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  bus.regs[cmd::REG_INPUT_PORT_0] = 0xAB;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0xCD;
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(3000, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_TRUE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);

  PortData data;
  TEST_ASSERT_TRUE(dev.getLastReadInputs(data).ok());
  TEST_ASSERT_EQUAL_HEX8(0xAB, data.port0);
  TEST_ASSERT_EQUAL_HEX8(0xCD, data.port1);

  st = dev.pollJob(3001, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertErrataWriteAt(bus, 1);
  TEST_ASSERT_EQUAL_UINT32(3001u, dev.lastOkMs());
}

void test_read_inputs_job_lock_hooks_do_not_hide_extra_i2c_instructions() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = true;
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(3500, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);

  st = dev.pollJob(3501, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertErrataWriteAt(bus, 1);
}

void test_read_inputs_job_with_errata_budget_2_completes_two_instructions() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = true;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(4000, 2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  assertErrataWriteAt(bus, 1);
}

void test_tick_advances_one_chunked_instruction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = true;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  dev.tick(4500);
  TEST_ASSERT_TRUE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);

  dev.tick(4501);
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_TRUE(dev.lastJobStatus().ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertErrataWriteAt(bus, 1);
  TEST_ASSERT_EQUAL_UINT32(4501u, dev.lastOkMs());
}

void test_read_inputs_job_read_failure_skips_pointer_park() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::I2C_TIMEOUT, "forced read timeout", -11);
  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(5000, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('R'), cmd::REG_INPUT_PORT_0, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(dev.lastJobStatus().code));
}

void test_read_inputs_job_pointer_park_failure_propagates_write_error() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced park nack", -12);
  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  Status st = dev.pollJob(6000, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('W'), cmd::ERRATA_SAFE_CMD, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.lastJobStatus().code));
}

void test_chunked_jobs_block_offline_without_backend_transfer() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  bus.readErrorRemaining = 1;
  PortData data;
  TEST_ASSERT_FALSE(dev.readInputs(data).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));
  resetFakeTransactionLog(bus);

  Status st = dev.startReadInputsJob();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
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

void test_failed_writes_do_not_update_cached_runtime_state() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  PortData data;
  data.port0 = 0x00;
  data.port1 = 0x11;

  bus.writeErrorRemaining = 1;
  Status st = dev.writeOutputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));

  SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_EQUAL_HEX8(0xFF, snapshot.config.outputPort0);
  TEST_ASSERT_EQUAL_HEX8(0xFF, snapshot.config.outputPort1);

  bus.writeErrorRemaining = 1;
  st = dev.setConfiguration(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  snapshot = dev.getSettings();
  TEST_ASSERT_EQUAL_HEX8(0xFF, snapshot.config.configPort0);
  TEST_ASSERT_EQUAL_HEX8(0xFF, snapshot.config.configPort1);

  bus.writeErrorRemaining = 1;
  st = dev.setPolarity(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_ERROR), static_cast<uint8_t>(st.code));
  snapshot = dev.getSettings();
  TEST_ASSERT_EQUAL_HEX8(0x00, snapshot.config.polarityPort0);
  TEST_ASSERT_EQUAL_HEX8(0x00, snapshot.config.polarityPort1);
}

void test_transport_in_progress_does_not_update_health() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorRemaining = 1;
  bus.writeError = Status{Err::IN_PROGRESS, 0, "queued"};
  Status st = dev.writeOutput(Port::PORT_0, 0x00);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::IN_PROGRESS), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0u, dev.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(dev.state()));

  const SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_EQUAL_HEX8(0xFF, snapshot.config.outputPort0);
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

  TEST_ASSERT_TRUE(dev.writePin(0, true).ok());
  TEST_ASSERT_EQUAL_HEX8(0xA1, bus.regs[cmd::REG_OUTPUT_PORT_0]);

  const uint8_t bulkCfg[2] = {0x0F, 0xF0};
  TEST_ASSERT_TRUE(dev.writeRegisters(cmd::REG_CONFIG_PORT_0, bulkCfg, 2).ok());
  const SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_EQUAL_HEX8(bulkCfg[0], snapshot.config.configPort0);
  TEST_ASSERT_EQUAL_HEX8(bulkCfg[1], snapshot.config.configPort1);
}

void test_bulk_register_helpers_wrap_odd_start_within_pair() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  const uint8_t values[2] = {0x12, 0x34};
  Status st = dev.writeRegisters(cmd::REG_OUTPUT_PORT_1, values, 2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0x34, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.regs[cmd::REG_OUTPUT_PORT_1]);

  uint8_t readback[2] = {};
  TEST_ASSERT_TRUE(dev.readRegisters(cmd::REG_OUTPUT_PORT_1, readback, 2).ok());
  TEST_ASSERT_EQUAL_HEX8(values[0], readback[0]);
  TEST_ASSERT_EQUAL_HEX8(values[1], readback[1]);

  TEST_ASSERT_TRUE(dev.writePin(0, true).ok());
  TEST_ASSERT_EQUAL_HEX8(0x35, bus.regs[cmd::REG_OUTPUT_PORT_0]);

  const uint8_t configValues[2] = {0xF0, 0x0F};
  TEST_ASSERT_TRUE(dev.writeRegisters(cmd::REG_CONFIG_PORT_1, configValues, 2).ok());
  const SettingsSnapshot snapshot = dev.getSettings();
  TEST_ASSERT_EQUAL_HEX8(0x0F, snapshot.config.configPort0);
  TEST_ASSERT_EQUAL_HEX8(0xF0, snapshot.config.configPort1);
}

void test_fake_transaction_log_records_address_payload_rx_status_and_pointer() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  setFakeAddress(bus, cfg, 0x23);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  resetFakeTransactionLog(bus);
  const uint8_t pointerBeforeWrite = bus.commandPointer;
  TEST_ASSERT_TRUE(dev.writeOutput(Port::PORT_1, 0x12).ok());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('W'), bus.transactions[0].type);
  TEST_ASSERT_EQUAL_HEX8(0x23, bus.transactions[0].address);
  TEST_ASSERT_EQUAL_UINT32(cfg.i2cTimeoutMs, bus.transactions[0].timeoutMs);
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactions[0].txLen);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_1, bus.transactions[0].tx[0]);
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.transactions[0].tx[1]);
  TEST_ASSERT_TRUE(bus.transactions[0].status.ok());
  TEST_ASSERT_EQUAL_HEX8(pointerBeforeWrite, bus.transactions[0].pointerBefore);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_1, bus.transactions[0].pointerAfter);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactions[0].dataBytesReachedHardware);

  resetFakeTransactionLog(bus);
  uint8_t value = 0;
  TEST_ASSERT_TRUE(dev.readOutput(Port::PORT_1, value).ok());
  TEST_ASSERT_EQUAL_HEX8(0x12, value);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('R'), bus.transactions[0].type);
  TEST_ASSERT_EQUAL_HEX8(0x23, bus.transactions[0].address);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactions[0].txLen);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_1, bus.transactions[0].tx[0]);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactions[0].rxRequested);
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactions[0].rxLen);
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.transactions[0].rx[0]);
  TEST_ASSERT_TRUE(bus.transactions[0].status.ok());
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_1, bus.transactions[0].pointerAfter);
}

void test_direct_register_command_matrix_reads_and_writes_exact_commands() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  fakeSetInputs(bus, PortData{0x33, 0xCC});
  fakeMutateOutputLatch(bus, PortData{0xA5, 0x5A});
  fakeMutatePolarity(bus, PortData{0x0F, 0xF0});
  fakeMutateConfiguration(bus, PortData{0x3C, 0xC3});

  for (uint8_t reg = cmd::REG_INPUT_PORT_0; reg < cmd::NUM_REGISTERS; ++reg) {
    resetFakeTransactionLog(bus);
    uint8_t value = 0;
    Status st = dev.readRegister(reg, value);
    TEST_ASSERT_TRUE(st.ok());
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('R'), bus.transactionType[0]);
    TEST_ASSERT_EQUAL_HEX8(reg, bus.transactions[0].tx[0]);
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactions[0].rxRequested);
    TEST_ASSERT_EQUAL_HEX8(fakeReadableRegisterValue(&bus, reg), value);
    TEST_ASSERT_EQUAL_HEX8(value, bus.transactions[0].rx[0]);
    TEST_ASSERT_TRUE(bus.transactions[0].status.ok());
  }

  for (uint8_t reg = cmd::REG_OUTPUT_PORT_0; reg < cmd::NUM_REGISTERS; ++reg) {
    const uint8_t value = static_cast<uint8_t>(0x80U | reg);
    resetFakeTransactionLog(bus);
    Status st = dev.writeRegister(reg, value);
    TEST_ASSERT_TRUE(st.ok());
    TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>('W'), bus.transactionType[0]);
    TEST_ASSERT_EQUAL_UINT(2u, bus.transactions[0].txLen);
    TEST_ASSERT_EQUAL_HEX8(reg, bus.transactions[0].tx[0]);
    TEST_ASSERT_EQUAL_HEX8(value, bus.transactions[0].tx[1]);
    TEST_ASSERT_EQUAL_HEX8(value, bus.regs[reg]);
    TEST_ASSERT_TRUE(bus.transactions[0].status.ok());
  }
}

void test_register_pair_auto_increment_wrap_matrix_for_all_pairs() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.applyInterruptErrata = false;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  for (uint8_t pairStart = 0; pairStart < cmd::NUM_REGISTERS; pairStart += 2U) {
    bus.regs[pairStart] = static_cast<uint8_t>(0x10U + pairStart);
    bus.regs[pairStart + 1U] = static_cast<uint8_t>(0x20U + pairStart);

    uint8_t readback[2] = {};
    TEST_ASSERT_TRUE(dev.readRegisters(pairStart, readback, 2).ok());
    TEST_ASSERT_EQUAL_HEX8(fakeReadableRegisterValue(&bus, pairStart), readback[0]);
    TEST_ASSERT_EQUAL_HEX8(fakeReadableRegisterValue(&bus, pairStart + 1U), readback[1]);

    readback[0] = 0;
    readback[1] = 0;
    TEST_ASSERT_TRUE(dev.readRegisters(static_cast<uint8_t>(pairStart + 1U), readback, 2).ok());
    TEST_ASSERT_EQUAL_HEX8(fakeReadableRegisterValue(&bus, pairStart + 1U), readback[0]);
    TEST_ASSERT_EQUAL_HEX8(fakeReadableRegisterValue(&bus, pairStart), readback[1]);
  }

  for (uint8_t pairStart = cmd::REG_OUTPUT_PORT_0;
       pairStart < cmd::NUM_REGISTERS;
       pairStart += 2U) {
    uint8_t values[2] = {
      static_cast<uint8_t>(0xA0U + pairStart),
      static_cast<uint8_t>(0xB0U + pairStart)
    };
    TEST_ASSERT_TRUE(dev.writeRegisters(pairStart, values, 2).ok());
    TEST_ASSERT_EQUAL_HEX8(values[0], bus.regs[pairStart]);
    TEST_ASSERT_EQUAL_HEX8(values[1], bus.regs[pairStart + 1U]);

    uint8_t oddValues[2] = {
      static_cast<uint8_t>(0xC0U + pairStart),
      static_cast<uint8_t>(0xD0U + pairStart)
    };
    TEST_ASSERT_TRUE(dev.writeRegisters(static_cast<uint8_t>(pairStart + 1U),
                                        oddValues, 2).ok());
    TEST_ASSERT_EQUAL_HEX8(oddValues[1], bus.regs[pairStart]);
    TEST_ASSERT_EQUAL_HEX8(oddValues[0], bus.regs[pairStart + 1U]);
  }
}

void test_fake_bus_keeps_input_register_pair_read_only() {
  FakeBus bus;
  fakeSetInputs(bus, PortData{0xAA, 0x55});
  const uint8_t payload[3] = {cmd::REG_INPUT_PORT_1, 0x00, 0x11};

  Status st = fakeWrite(cmd::BASE_ADDRESS, payload, sizeof(payload), 10, &bus);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_HEX8(0xAA, bus.regs[cmd::REG_INPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x55, bus.regs[cmd::REG_INPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_INPUT_PORT_1, bus.commandPointer);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactions[0].dataBytesReachedHardware);
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

void test_configure_outputs_writes_latch_before_config() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  Status st = dev.configureOutputs(0x0104, 0x0100);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.transactionReg[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.transactionData0[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.transactionData1[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.transactionReg[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.transactionData0[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFE, bus.transactionData1[1]);
}

void test_single_pin_output_transition_writes_latch_before_config() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(dev.preloadOutput(2, false).ok());
  resetFakeTransactionLog(bus);

  Status st = dev.setDirection(2, Direction::OUTPUT_MODE);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.transactionReg[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.transactionData0[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.transactionData1[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.transactionReg[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.transactionData0[1]);
}

void test_forced_preload_writes_even_when_cache_matches() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  Status st = dev.preloadOutput(1, true);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.transactionReg[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.transactionData0[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_OUTPUT, bus.transactionData1[0]);
}

void test_failed_preload_does_not_change_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced preload timeout", -40);
  Status st = dev.configureOutputs(0x0004, 0x0000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, dev.getSettings().config.configPort0);
}

void test_failed_direction_after_preload_marks_dirty() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  resetFakeTransactionLog(bus);
  bus.writeErrorOnCall = bus.writeCalls + 2;
  bus.writeError = Status::Error(Err::I2C_BUS, "forced direction write", -41);
  Status st = dev.configureOutputs(0x0004, 0x0000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_OUTPUT_PORT_0, bus.transactionReg[0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.transactionReg[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFB, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
}

void test_write_outputs_job_noop_is_cpu_only() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  const uint32_t successBefore = dev.totalSuccess();
  const uint32_t failuresBefore = dev.totalFailures();
  const uint32_t lastOkBefore = dev.lastOkMs();

  Status st = dev.startWriteOutputsJob(0xFFFF, 0xFFFF);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_TRUE(dev.pollJob(7000, 1).ok());
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT32(successBefore, dev.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(failuresBefore, dev.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(lastOkBefore, dev.lastOkMs());
}

void test_write_outputs_job_mask_writes_one_output_pair_instruction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startWriteOutputsJob(0x00F0, 0x0000).inProgress());
  Status st = dev.pollJob(7100, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('W'), cmd::REG_OUTPUT_PORT_0, 3);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.transactionData0[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.transactionData1[0]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_UINT32(7100u, dev.lastOkMs());
}

void test_configure_outputs_job_budget_1_preloads_latch_before_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startConfigureOutputsJob(0x00F0, 0x0000).inProgress());
  Status st = dev.pollJob(8000, 1);
  TEST_ASSERT_TRUE(st.inProgress());
  TEST_ASSERT_TRUE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('W'), cmd::REG_OUTPUT_PORT_0, 3);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);

  st = dev.pollJob(8001, 1);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('W'), cmd::REG_CONFIG_PORT_0, 3);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_configure_outputs_job_budget_2_writes_latch_then_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startConfigureOutputsJob(0x0F00, 0x0000).inProgress());
  Status st = dev.pollJob(8100, 2);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('W'), cmd::REG_OUTPUT_PORT_0, 3);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('W'), cmd::REG_CONFIG_PORT_0, 3);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.regs[cmd::REG_CONFIG_PORT_1]);
}

void test_configure_outputs_job_latch_failure_skips_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_BUS, "forced latch write failure", -13);
  TEST_ASSERT_TRUE(dev.startConfigureOutputsJob(0x00F0, 0x0000).inProgress());
  Status st = dev.pollJob(8200, 2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_BUS),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('W'), cmd::REG_OUTPUT_PORT_0, 3);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_configure_outputs_job_direction_failure_propagates_after_latch() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startConfigureOutputsJob(0x00F0, 0x0000).inProgress());
  TEST_ASSERT_TRUE(dev.pollJob(8300, 1).inProgress());
  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced config write failure", -14);
  Status st = dev.pollJob(8301, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(st.code));
  TEST_ASSERT_FALSE(dev.jobActive());
  TEST_ASSERT_EQUAL_UINT(2u, bus.transactionCount);
  assertFakeEvent(bus, 0, static_cast<uint8_t>('W'), cmd::REG_OUTPUT_PORT_0, 3);
  assertFakeEvent(bus, 1, static_cast<uint8_t>('W'), cmd::REG_CONFIG_PORT_0, 3);
  TEST_ASSERT_EQUAL_HEX8(0x0F, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
}

void test_active_job_blocks_synchronous_i2c_helpers() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  PortData outputs = PortData::fromCombined(0x0000);
  Status st = dev.writeOutputs(outputs);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);

  TEST_ASSERT_TRUE(dev.pollJob(8400, 2).ok());
}

void test_active_job_blocks_synchronous_input_before_lock() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  enableFakeLock(cfg, bus);
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeLockStats(bus);
  resetFakeTransactionLog(bus);

  TEST_ASSERT_TRUE(dev.startReadInputsJob().inProgress());
  PortData data;
  Status st = dev.readInputs(data);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::BUSY), static_cast<uint8_t>(st.code));
  TEST_ASSERT_EQUAL_UINT32(0u, bus.lockCalls);
  TEST_ASSERT_EQUAL_UINT32(0u, bus.unlockCalls);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);

  TEST_ASSERT_TRUE(dev.pollJob(8500, 2).ok());
}

void test_output_to_input_transition_writes_config_only() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.configPort0 = 0xFB;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeTransactionLog(bus);

  Status st = dev.setDirection(2, Direction::INPUT_MODE);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT(1u, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(cmd::REG_CONFIG_PORT_0, bus.transactionReg[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.transactionData0[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
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

void test_polarity_inverts_input_sense_only_not_output_latch_or_direction() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  fakeSetInputs(bus, PortData{0x0F, 0xF0});
  TEST_ASSERT_TRUE(dev.setPolarity(PortData{0xFF, 0x0F}).ok());
  TEST_ASSERT_TRUE(dev.writeOutputs(PortData{0xAA, 0x55}).ok());

  PortData inputs;
  TEST_ASSERT_TRUE(dev.readInputs(inputs).ok());
  TEST_ASSERT_EQUAL_HEX8(0xF0, inputs.port0);
  TEST_ASSERT_EQUAL_HEX8(0xFF, inputs.port1);

  uint8_t output = 0;
  TEST_ASSERT_TRUE(dev.readOutput(Port::PORT_0, output).ok());
  TEST_ASSERT_EQUAL_HEX8(0xAA, output);
  TEST_ASSERT_TRUE(dev.readOutput(Port::PORT_1, output).ok());
  TEST_ASSERT_EQUAL_HEX8(0x55, output);

  uint8_t direction = 0;
  TEST_ASSERT_TRUE(dev.getPortConfiguration(Port::PORT_0, direction).ok());
  TEST_ASSERT_EQUAL_HEX8(cmd::DEFAULT_CONFIG, direction);
}

void test_output_latch_writes_do_not_mutate_input_sense() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  TEST_ASSERT_TRUE(dev.begin(makeConfig(bus)).ok());

  fakeSetInputs(bus, PortData{0x5A, 0xA5});
  TEST_ASSERT_TRUE(dev.writeOutputs(PortData{0x00, 0xFF}).ok());

  PortData inputs;
  TEST_ASSERT_TRUE(dev.readInputs(inputs).ok());
  TEST_ASSERT_EQUAL_HEX8(0x5A, inputs.port0);
  TEST_ASSERT_EQUAL_HEX8(0xA5, inputs.port1);
  TEST_ASSERT_EQUAL_HEX8(0x00, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_1]);
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

  fakePowerCycle(bus);

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

  const uint32_t readsBefore = bus.readCalls;
  const uint32_t writesBefore = bus.writeCalls;
  uint8_t value = 0;
  Status st = dev.readRegister(0x08, value);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  st = dev.writeRegister(0x08, 0x55);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  uint8_t buf[3] = {};
  st = dev.readRegisters(cmd::REG_OUTPUT_PORT_0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  st = dev.writeRegisters(cmd::REG_OUTPUT_PORT_0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::INVALID_PARAM),
                          static_cast<uint8_t>(st.code));

  TEST_ASSERT_EQUAL_UINT32(readsBefore, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
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
  uint16_t combined = 0;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.readInputsAndClearInterrupt(combined).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.clearInterrupts().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.applyInterruptErrataWorkaround().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.applyInterruptErrataWorkaroundUnlocked().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startReadInputsJob().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startWriteOutputsJob(0xFFFF, 0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.startConfigureOutputsJob(0xFFFF, 0).code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::NOT_INITIALIZED),
                          static_cast<uint8_t>(dev.pollJob(0, 1).code));
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

void test_end_safe_state_write_failure_remains_observable() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.configPort0 = 0x00;
  cfg.configPort1 = 0x00;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_TIMEOUT, "forced end timeout", -18);
  dev.end();

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_TIMEOUT),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
  TEST_ASSERT_EQUAL_INT32(-18, dev.hardwareStateDirtyError().detail);
}

void test_end_while_offline_does_not_touch_bus() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -13);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t writesBefore = bus.writeCalls;
  dev.end();
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
}

void test_end_while_offline_preserves_existing_dirty_state() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.writeErrorRemaining = 1;
  bus.writeError = Status::Error(Err::I2C_NACK_DATA, "forced dirty write", -19);
  PortData outputs{0x00, 0x00};
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.writeOutputs(outputs).code));
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -20);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t writesBefore = bus.writeCalls;
  dev.end();

  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::UNINIT),
                          static_cast<uint8_t>(dev.state()));
  TEST_ASSERT_TRUE(dev.hardwareStateDirty());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::I2C_NACK_DATA),
                          static_cast<uint8_t>(dev.hardwareStateDirtyError().code));
  TEST_ASSERT_EQUAL_INT32(-19, dev.hardwareStateDirtyError().detail);
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

void test_configure_output_bits_no_op_for_existing_outputs() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.configPort1 = 0xF0;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());
  resetFakeTransactionLog(bus);

  const uint32_t writesBefore = bus.writeCalls;
  Status st = dev.configureOutputBits(0x0F00);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(writesBefore, bus.writeCalls);
  TEST_ASSERT_EQUAL_UINT(0u, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(0xF0, bus.regs[cmd::REG_CONFIG_PORT_1]);
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

void test_noop_mutators_block_offline_without_i2c() {
  FakeBus bus;
  PCA9555::PCA9555 dev;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 1;
  TEST_ASSERT_TRUE(dev.begin(cfg).ok());

  bus.readErrorRemaining = 1;
  bus.readError = Status::Error(Err::TIMEOUT, "forced timeout", -17);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Err::TIMEOUT),
                          static_cast<uint8_t>(dev.recover().code));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::OFFLINE),
                          static_cast<uint8_t>(dev.state()));

  const uint32_t readsBefore = bus.readCalls;
  const uint32_t writesBefore = bus.writeCalls;

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.writePin(0, true), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.preloadOutputs(0x0000, 0x0000), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.setOutputBits(0xFFFF), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.clearOutputBits(0x0000), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.toggleOutputBits(0x0000), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.configureInputBits(0xFFFF), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.configureOutputBits(0x0000), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.setInvertBits(0x0000), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.clearInvertBits(0xFFFF), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.setPinPolarity(0, false), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.setPinDirection(0, true), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.configureOutputs(0x0000, 0x0000), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.startWriteOutputsJob(0x0000, 0x0000), readsBefore, writesBefore);

  resetFakeTransactionLog(bus);
  assertBusyNoI2c(bus, dev.startConfigureOutputsJob(0x0000, 0x0000), readsBefore, writesBefore);
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
  RUN_TEST(test_begin_accepts_all_pca9555_address_pins_and_logs_callback_address);
  RUN_TEST(test_begin_rejects_address_matrix_without_touching_bus);
  RUN_TEST(test_begin_reports_device_absent_or_wrong_address_as_not_found);
  RUN_TEST(test_begin_presence_read_preserves_non_address_transport_errors);
  RUN_TEST(test_begin_rejects_zero_timeout);
  RUN_TEST(test_begin_rejects_partial_lock_hooks);
  RUN_TEST(test_failed_begin_clears_stale_runtime_snapshot);
  RUN_TEST(test_failed_begin_apply_clears_runtime_snapshot);
  RUN_TEST(test_begin_success_sets_ready_and_health);
  RUN_TEST(test_get_settings_snapshot_reflects_runtime_state);
  RUN_TEST(test_begin_rejects_non_default_config_ports_by_default);
  RUN_TEST(test_begin_checks_both_configuration_defaults_not_input_identity);
  RUN_TEST(test_begin_allows_non_default_config_ports_when_check_disabled);
  RUN_TEST(test_begin_applies_config_to_device);
  RUN_TEST(test_begin_ordering_remains_safe);

  // Health timestamps
  RUN_TEST(test_null_now_ms_keeps_health_timestamps_zero);
  RUN_TEST(test_now_ms_callback_updates_health_timestamps);

  // probe / recover / health
  RUN_TEST(test_probe_failure_does_not_update_health);
  RUN_TEST(test_probe_error_matrix_preserves_transport_error_kind);
  RUN_TEST(test_recover_failure_updates_health_once);
  RUN_TEST(test_recover_success_returns_ready);
  RUN_TEST(test_recover_preserves_transport_error_code);
  RUN_TEST(test_recover_reaches_offline_when_threshold_is_one);
  RUN_TEST(test_offline_latches_normal_read_without_i2c_until_recover);
  RUN_TEST(test_probe_blocks_offline_without_i2c);
  RUN_TEST(test_offline_input_read_checks_latch_before_lock);
  RUN_TEST(test_failed_recover_from_offline_preserves_latch_after_partial_success);
  RUN_TEST(test_failed_recover_from_offline_preserves_latch_on_in_progress);
  RUN_TEST(test_failure_threshold_enters_offline_and_blocks_bus_until_recover);

  // Hardware dirty-state diagnostics
  RUN_TEST(test_failed_validation_does_not_mark_hardware_dirty);
  RUN_TEST(test_failed_read_does_not_mark_hardware_dirty);
  RUN_TEST(test_fail_before_apply_write_marks_dirty_without_cache_update);
  RUN_TEST(test_partial_output_pair_write_marks_dirty_and_preserves_error);
  RUN_TEST(test_partial_configuration_pair_write_marks_dirty_and_preserves_error);
  RUN_TEST(test_partial_polarity_pair_write_marks_dirty_and_preserves_error);
  RUN_TEST(test_direct_register_write_failure_marks_dirty);
  RUN_TEST(test_direct_odd_start_pair_write_failure_marks_dirty);
  RUN_TEST(test_hardware_dirty_status_appears_in_settings_snapshot);
  RUN_TEST(test_hardware_dirty_survives_unrelated_successful_reads);
  RUN_TEST(test_hardware_dirty_clears_after_full_successful_recover);
  RUN_TEST(test_hardware_dirty_does_not_clear_after_partial_recover);
  RUN_TEST(test_begin_validation_failure_preserves_existing_hardware_dirty);
  RUN_TEST(test_failed_begin_apply_partial_write_marks_dirty_and_uninitialized);
  RUN_TEST(test_transport_read_error_matrix_updates_health_without_dirty_state);
  RUN_TEST(test_transport_write_error_matrix_updates_health_and_marks_dirty);
  RUN_TEST(test_partial_pair_write_all_bytes_then_error_marks_dirty_without_cache_sync);
  RUN_TEST(test_short_write_failures_record_command_boundary);
  RUN_TEST(test_short_read_and_unavailable_data_do_not_sync_cache_on_failure);

  // Example transport
  RUN_TEST(test_example_transport_maps_wire_errors);
  RUN_TEST(test_example_transport_write_read_maps_wire_errors_and_short_read);
  RUN_TEST(test_example_transport_validates_params);

  // Input/Output/Config API
  RUN_TEST(test_read_inputs_returns_port_data);
  RUN_TEST(test_read_inputs_applies_errata_workaround_when_enabled);
  RUN_TEST(test_read_inputs_skips_errata_workaround_when_disabled);
  RUN_TEST(test_read_register_input_port_applies_errata_workaround);
  RUN_TEST(test_all_input_read_paths_write_exact_errata_command);
  RUN_TEST(test_read_inputs_errata_write_failure_is_reported_and_updates_health);
  RUN_TEST(test_read_inputs_read_failure_does_not_pointer_park);
  RUN_TEST(test_read_inputs_and_clear_interrupt_returns_both_ports);
  RUN_TEST(test_clear_interrupts_reads_both_ports_then_parks_pointer);
  RUN_TEST(test_clear_interrupts_errata_disabled_reads_only);
  RUN_TEST(test_read_input_port0_clears_only_port0_interrupt);
  RUN_TEST(test_read_input_port1_clears_only_port1_interrupt);
  RUN_TEST(test_apply_interrupt_errata_workaround_parks_pointer);
  RUN_TEST(test_apply_interrupt_errata_workaround_locked_variant_uses_hooks);
  RUN_TEST(test_apply_interrupt_errata_workaround_unlocked_variant_skips_hooks);
  RUN_TEST(test_input_read_errata_lock_wraps_full_sequence);
  RUN_TEST(test_input_read_errata_lock_releases_on_read_failure);
  RUN_TEST(test_input_read_errata_lock_releases_on_errata_failure);
  RUN_TEST(test_input_read_lock_failure_skips_i2c_and_unlock);
  RUN_TEST(test_input_read_validation_failure_does_not_lock);
  RUN_TEST(test_input_read_errata_lock_blocks_interleaved_external_read);
  RUN_TEST(test_read_inputs_job_without_errata_budget_1_completes_one_read);
  RUN_TEST(test_read_inputs_job_with_errata_budget_1_splits_read_and_pointer_park);
  RUN_TEST(test_read_inputs_job_lock_hooks_do_not_hide_extra_i2c_instructions);
  RUN_TEST(test_read_inputs_job_with_errata_budget_2_completes_two_instructions);
  RUN_TEST(test_tick_advances_one_chunked_instruction);
  RUN_TEST(test_read_inputs_job_read_failure_skips_pointer_park);
  RUN_TEST(test_read_inputs_job_pointer_park_failure_propagates_write_error);
  RUN_TEST(test_chunked_jobs_block_offline_without_backend_transfer);
  RUN_TEST(test_read_pin_returns_correct_bit);
  RUN_TEST(test_read_pin_rejects_invalid_pin);
  RUN_TEST(test_single_pin_helpers_reject_invalid_pin);
  RUN_TEST(test_port_apis_reject_invalid_port_enum);
  RUN_TEST(test_write_outputs_updates_device);
  RUN_TEST(test_failed_writes_do_not_update_cached_runtime_state);
  RUN_TEST(test_transport_in_progress_does_not_update_health);
  RUN_TEST(test_bulk_register_helpers_round_trip_and_update_shadow);
  RUN_TEST(test_bulk_register_helpers_wrap_odd_start_within_pair);
  RUN_TEST(test_fake_transaction_log_records_address_payload_rx_status_and_pointer);
  RUN_TEST(test_direct_register_command_matrix_reads_and_writes_exact_commands);
  RUN_TEST(test_register_pair_auto_increment_wrap_matrix_for_all_pairs);
  RUN_TEST(test_fake_bus_keeps_input_register_pair_read_only);
  RUN_TEST(test_bulk_read_input_registers_applies_errata_workaround);
  RUN_TEST(test_write_pin_modifies_single_bit);
  RUN_TEST(test_write_pin_port1);
  RUN_TEST(test_read_output_and_output_pin_return_latched_state);
  RUN_TEST(test_write_pin_no_op_if_already_set);
  RUN_TEST(test_set_configuration_updates_device);
  RUN_TEST(test_configure_outputs_writes_latch_before_config);
  RUN_TEST(test_single_pin_output_transition_writes_latch_before_config);
  RUN_TEST(test_forced_preload_writes_even_when_cache_matches);
  RUN_TEST(test_failed_preload_does_not_change_direction);
  RUN_TEST(test_failed_direction_after_preload_marks_dirty);
  RUN_TEST(test_write_outputs_job_noop_is_cpu_only);
  RUN_TEST(test_write_outputs_job_mask_writes_one_output_pair_instruction);
  RUN_TEST(test_configure_outputs_job_budget_1_preloads_latch_before_direction);
  RUN_TEST(test_configure_outputs_job_budget_2_writes_latch_then_direction);
  RUN_TEST(test_configure_outputs_job_latch_failure_skips_direction);
  RUN_TEST(test_configure_outputs_job_direction_failure_propagates_after_latch);
  RUN_TEST(test_active_job_blocks_synchronous_i2c_helpers);
  RUN_TEST(test_active_job_blocks_synchronous_input_before_lock);
  RUN_TEST(test_output_to_input_transition_writes_config_only);
  RUN_TEST(test_set_pin_direction);
  RUN_TEST(test_get_port_configuration_and_pin_direction);
  RUN_TEST(test_set_polarity);
  RUN_TEST(test_set_pin_polarity);
  RUN_TEST(test_get_port_polarity_and_pin_polarity);
  RUN_TEST(test_polarity_inverts_input_sense_only_not_output_latch_or_direction);
  RUN_TEST(test_output_latch_writes_do_not_mutate_input_sense);
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
  RUN_TEST(test_configure_output_bits_no_op_for_existing_outputs);
  RUN_TEST(test_set_invert_bits);
  RUN_TEST(test_clear_invert_bits);
  RUN_TEST(test_bit_manipulation_no_op_skips_i2c);
  RUN_TEST(test_noop_mutators_block_offline_without_i2c);
  RUN_TEST(test_bit_manipulation_rejects_before_begin);

  // PortData
  RUN_TEST(test_port_data_combined);
  RUN_TEST(test_port_data_from_combined);

  // Not-initialized guards
  RUN_TEST(test_operations_reject_before_begin);
  RUN_TEST(test_end_sets_safe_input_state);
  RUN_TEST(test_end_safe_state_write_failure_remains_observable);
  RUN_TEST(test_end_while_offline_does_not_touch_bus);
  RUN_TEST(test_end_while_offline_preserves_existing_dirty_state);

  return UNITY_END();
}

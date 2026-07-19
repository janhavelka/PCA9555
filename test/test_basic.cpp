/// @file test_basic.cpp
/// @brief Native behavior and fault-injection tests for the passive PCA9555 driver.

#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <unity.h>

#include "PCA9555/PCA9555.h"

using namespace PCA9555;

namespace {

static constexpr size_t FAKE_LOG_CAPACITY = 128U;
static constexpr size_t FAKE_BUFFER_CAPACITY = 8U;

struct FakeTransaction {
  char type = '?';
  uint8_t address = 0;
  uint8_t reg = 0;
  uint32_t timeoutMs = 0;
  uint8_t tx[FAKE_BUFFER_CAPACITY] = {};
  size_t txLen = 0;
  uint8_t rx[FAKE_BUFFER_CAPACITY] = {};
  size_t rxRequested = 0;
  size_t rxProduced = 0;
  size_t dataBytesApplied = 0;
  TransportResult result{};
};

struct FakeFault {
  bool armed = false;
  char type = '?';
  uint8_t reg = 0;
  TransportResult result{};
  size_t dataBytesToApply = 0;
  size_t rxBytesToProduce = 0;
  size_t txLen = 0;  // Zero matches any length.
};

struct FakeBus {
  uint32_t nowMs = 1000U;
  uint8_t address = cmd::BASE_ADDRESS;
  bool present = true;
  bool enforceAddress = true;
  uint8_t pointer = 0;
  uint32_t writeCalls = 0;
  uint32_t readCalls = 0;
  size_t transactionCount = 0;
  FakeTransaction transactions[FAKE_LOG_CAPACITY] = {};
  FakeFault fault{};

  uint8_t regs[8] = {
      0xFF, 0xFF,  // Inputs, externally driven in the fake.
      0xFF, 0xFF,  // Output latches.
      0x00, 0x00,  // Polarity.
      0xFF, 0xFF   // Directions: all inputs.
  };
};

uint8_t pairedRegisterAt(uint8_t startReg, size_t offset) {
  return static_cast<uint8_t>((startReg & 0xFEU) |
      ((startReg + static_cast<uint8_t>(offset)) & 0x01U));
}

uint8_t readableRegister(const FakeBus& bus, uint8_t reg) {
  if (reg == cmd::REG_INPUT_PORT_0) {
    return static_cast<uint8_t>(bus.regs[reg] ^ bus.regs[cmd::REG_POLARITY_INV_0]);
  }
  if (reg == cmd::REG_INPUT_PORT_1) {
    return static_cast<uint8_t>(bus.regs[reg] ^ bus.regs[cmd::REG_POLARITY_INV_1]);
  }
  return reg <= cmd::REG_CONFIG_PORT_1 ? bus.regs[reg] : 0xFFU;
}

void copyBytes(uint8_t* dst, size_t capacity, const uint8_t* src, size_t len) {
  if (dst == nullptr || src == nullptr) {
    return;
  }
  const size_t count = len < capacity ? len : capacity;
  for (size_t i = 0; i < count; ++i) {
    dst[i] = src[i];
  }
}

FakeTransaction* beginTransaction(FakeBus& bus, char type, uint8_t address,
                                  uint32_t timeoutMs, const uint8_t* tx,
                                  size_t txLen, size_t rxRequested) {
  if (bus.transactionCount >= FAKE_LOG_CAPACITY) {
    return nullptr;
  }
  FakeTransaction& transaction = bus.transactions[bus.transactionCount++];
  transaction = FakeTransaction{};
  transaction.type = type;
  transaction.address = address;
  transaction.timeoutMs = timeoutMs;
  transaction.reg = tx != nullptr && txLen > 0U ? tx[0] : 0U;
  transaction.txLen = txLen;
  transaction.rxRequested = rxRequested;
  copyBytes(transaction.tx, FAKE_BUFFER_CAPACITY, tx, txLen);
  return &transaction;
}

size_t applyWriteData(FakeBus& bus, const uint8_t* data, size_t len,
                      size_t maximumDataBytes) {
  if (data == nullptr || len < 2U || data[0] > cmd::REG_CONFIG_PORT_1) {
    return 0U;
  }
  const size_t available = len - 1U;
  const size_t count = maximumDataBytes < available ? maximumDataBytes : available;
  size_t applied = 0U;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t reg = pairedRegisterAt(data[0], i);
    if (reg != cmd::REG_INPUT_PORT_0 && reg != cmd::REG_INPUT_PORT_1) {
      bus.regs[reg] = data[i + 1U];
      ++applied;
    }
  }
  return applied;
}

void fillReadData(FakeBus& bus, uint8_t startReg, uint8_t* rx, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    rx[i] = readableRegister(bus, pairedRegisterAt(startReg, i));
  }
}

bool matchingFault(const FakeBus& bus, char type, uint8_t reg, size_t txLen) {
  return bus.fault.armed && bus.fault.type == type && bus.fault.reg == reg &&
      (bus.fault.txLen == 0U || bus.fault.txLen == txLen);
}

TransportResult fakeWrite(uint8_t address, const uint8_t* data, size_t len,
                          uint32_t timeoutMs, void* user) {
  FakeBus& bus = *static_cast<FakeBus*>(user);
  ++bus.writeCalls;
  FakeTransaction* transaction =
      beginTransaction(bus, 'W', address, timeoutMs, data, len, 0U);
  if (data == nullptr || len == 0U) {
    return TransportResult::Error(TransportCode::IO_ERROR, -100,
                                  WriteEffect::NOT_ATTEMPTED);
  }
  if (!bus.present || (bus.enforceAddress && address != bus.address)) {
    const TransportResult result = TransportResult::Error(
        TransportCode::NACK_ADDRESS, -101, WriteEffect::NOT_ATTEMPTED);
    if (transaction != nullptr) transaction->result = result;
    return result;
  }

  bus.pointer = data[0];
  if (matchingFault(bus, 'W', data[0], len)) {
    const FakeFault fault = bus.fault;
    bus.fault.armed = false;
    const size_t applied = applyWriteData(bus, data, len, fault.dataBytesToApply);
    if (transaction != nullptr) {
      transaction->dataBytesApplied = applied;
      transaction->result = fault.result;
    }
    return fault.result;
  }

  const size_t applied = applyWriteData(bus, data, len, len - 1U);
  const TransportResult result{TransportCode::OK, 0, WriteEffect::COMMITTED,
                               len, 0U};
  if (transaction != nullptr) {
    transaction->dataBytesApplied = applied;
    transaction->result = result;
  }
  return result;
}

TransportResult fakeWriteRead(uint8_t address, const uint8_t* tx, size_t txLen,
                              uint8_t* rx, size_t rxLen, uint32_t timeoutMs,
                              void* user) {
  FakeBus& bus = *static_cast<FakeBus*>(user);
  ++bus.readCalls;
  FakeTransaction* transaction =
      beginTransaction(bus, 'R', address, timeoutMs, tx, txLen, rxLen);
  if (tx == nullptr || txLen == 0U || rx == nullptr || rxLen == 0U) {
    return TransportResult::Error(TransportCode::IO_ERROR, -102,
                                  WriteEffect::NOT_APPLICABLE);
  }
  if (!bus.present || (bus.enforceAddress && address != bus.address)) {
    const TransportResult result = TransportResult::Error(
        TransportCode::NACK_ADDRESS, -103, WriteEffect::NOT_APPLICABLE);
    if (transaction != nullptr) transaction->result = result;
    return result;
  }

  bus.pointer = tx[0];
  if (matchingFault(bus, 'R', tx[0], txLen)) {
    const FakeFault fault = bus.fault;
    bus.fault.armed = false;
    const size_t produced = fault.rxBytesToProduce < rxLen
        ? fault.rxBytesToProduce : rxLen;
    fillReadData(bus, tx[0], rx, produced);
    if (transaction != nullptr) {
      transaction->rxProduced = produced;
      copyBytes(transaction->rx, FAKE_BUFFER_CAPACITY, rx, produced);
      transaction->result = fault.result;
    }
    return fault.result;
  }

  fillReadData(bus, tx[0], rx, rxLen);
  const TransportResult result = TransportResult::Ok(txLen, rxLen);
  if (transaction != nullptr) {
    transaction->rxProduced = rxLen;
    copyBytes(transaction->rx, FAKE_BUFFER_CAPACITY, rx, rxLen);
    transaction->result = result;
  }
  return result;
}

uint32_t fakeNowMs(void* user) {
  return static_cast<FakeBus*>(user)->nowMs;
}

Config makeConfig(FakeBus& bus) {
  Config config;
  config.i2cWrite = fakeWrite;
  config.i2cWriteRead = fakeWriteRead;
  config.i2cUser = &bus;
  config.nowMs = fakeNowMs;
  config.timeUser = &bus;
  config.i2cAddress = bus.address;
  config.i2cTimeoutMs = 10U;
  return config;
}

void clearTransactions(FakeBus& bus) {
  bus.transactionCount = 0U;
}

uint32_t busTraffic(const FakeBus& bus) {
  return bus.readCalls + bus.writeCalls;
}

void armWriteFault(FakeBus& bus, uint8_t reg, TransportCode code,
                   WriteEffect effect, size_t dataBytesApplied,
                   int32_t detail = -200) {
  const size_t completedTxBytes = effect == WriteEffect::NOT_ATTEMPTED
      ? 0U : 1U + dataBytesApplied;
  bus.fault = FakeFault{
      true, 'W', reg,
      TransportResult::Error(code, detail, effect, completedTxBytes, 0U),
      dataBytesApplied, 0U};
}

void armReadFault(FakeBus& bus, uint8_t reg, TransportCode code,
                  size_t rxBytesProduced = 0U, int32_t detail = -201) {
  bus.fault = FakeFault{
      true, 'R', reg,
      TransportResult::Error(code, detail, WriteEffect::NOT_APPLICABLE,
                             1U, rxBytesProduced),
      0U, rxBytesProduced};
}

void fakePowerCycle(FakeBus& bus) {
  bus.regs[cmd::REG_OUTPUT_PORT_0] = cmd::DEFAULT_OUTPUT;
  bus.regs[cmd::REG_OUTPUT_PORT_1] = cmd::DEFAULT_OUTPUT;
  bus.regs[cmd::REG_POLARITY_INV_0] = cmd::DEFAULT_POLARITY;
  bus.regs[cmd::REG_POLARITY_INV_1] = cmd::DEFAULT_POLARITY;
  bus.regs[cmd::REG_CONFIG_PORT_0] = cmd::DEFAULT_CONFIG;
  bus.regs[cmd::REG_CONFIG_PORT_1] = cmd::DEFAULT_CONFIG;
  bus.pointer = 0U;
}

RegisterImage image(uint16_t outputs = 0xA55AU,
                    uint16_t polarity = 0x1122U,
                    uint16_t directions = 0xF00FU) {
  return RegisterImage{outputs, polarity, directions};
}

void assertTransaction(const FakeBus& bus, size_t index, char type,
                       uint8_t reg) {
  TEST_ASSERT_TRUE(index < bus.transactionCount);
  TEST_ASSERT_EQUAL_CHAR(type, bus.transactions[index].type);
  TEST_ASSERT_EQUAL_HEX8(reg, bus.transactions[index].reg);
}

OperationResult takeResult(PCA9555::PCA9555& device, uint32_t requestId) {
  OperationResult result;
  TEST_ASSERT_TRUE(device.takeOperationResult(requestId, result).ok());
  TEST_ASSERT_EQUAL_UINT32(requestId, result.requestId);
  return result;
}

OperationResult applyImage(PCA9555::PCA9555& device, FakeBus& bus,
                           const RegisterImage& expected,
                           uint32_t requestId = 1U,
                           uint32_t nowMs = 100U) {
  (void)bus;
  TEST_ASSERT_TRUE(device.startApplyImage(requestId, expected, nowMs, 100U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(requestId, nowMs, 8U, used).ok());
  TEST_ASSERT_EQUAL_UINT8(8U, used);
  return takeResult(device, requestId);
}

void bindAndApply(PCA9555::PCA9555& device, FakeBus& bus,
                  const RegisterImage& expected = image()) {
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  const OperationResult result = applyImage(device, bus, expected);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                          static_cast<uint8_t>(result.outcome));
}

void assertBusSilentSince(const FakeBus& bus, uint32_t before) {
  TEST_ASSERT_EQUAL_UINT32(before, busTraffic(bus));
}

Status attemptCachedRmw(PCA9555::PCA9555& device, uint8_t pair) {
  if (pair == PAIR_OUTPUTS) return device.togglePin(Pin::P00);
  if (pair == PAIR_POLARITY) return device.setInvertBits(pinMask(Pin::P00));
  return device.configureInputBits(pinMask(Pin::P04));
}

uint8_t allWritableExcept(uint8_t pair) {
  return static_cast<uint8_t>(
      static_cast<uint8_t>(PAIR_ALL_WRITABLE) & static_cast<uint8_t>(~pair));
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_status_and_typed_value_helpers() {
  constexpr PortData ports = PortData::fromCombined(0xA55AU);
  TEST_ASSERT_EQUAL_HEX8(0x5A, ports.port0);
  TEST_ASSERT_EQUAL_HEX8(0xA5, ports.port1);
  TEST_ASSERT_EQUAL_HEX16(0xA55A, ports.combined());
  TEST_ASSERT_TRUE(Status::Ok().ok());
  TEST_ASSERT_TRUE(Status::Error(Err::I2C_BUS, "bus", 7).is(Err::I2C_BUS));
  TEST_ASSERT_EQUAL_STRING("OK", errorName(Err::OK));
  TEST_ASSERT_EQUAL_STRING("I2C_NACK_ADDR", errorName(Err::I2C_NACK_ADDR));
  TEST_ASSERT_EQUAL_STRING("OFFLINE_RESERVED", errorName(Err::OFFLINE));
  TEST_ASSERT_EQUAL_STRING("UNSUPPORTED", errorName(Err::UNSUPPORTED));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", errorName(static_cast<Err>(0xFFU)));

  const RegisterImage expected{0x8001U, 0U, 0xFFFEU};
  TEST_ASSERT_EQUAL_UINT8(0U, pinIndex(Pin::P00));
  TEST_ASSERT_EQUAL_UINT8(15U, pinIndex(Pin::P17));
  TEST_ASSERT_EQUAL_HEX16(0x0001, pinMask(Pin::P00));
  TEST_ASSERT_EQUAL_HEX16(0x8000, pinMask(Pin::P17));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Port::PORT_1),
                          static_cast<uint8_t>(portOf(Pin::P10)));
  TEST_ASSERT_EQUAL_UINT8(7U, bitOf(Pin::P17));
  TEST_ASSERT_TRUE(isOutput(expected, Pin::P00));
  TEST_ASSERT_FALSE(isOutput(expected, Pin::P17));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Level::HIGH_LEVEL),
                          static_cast<uint8_t>(levelFor(expected, Pin::P17)));
  TEST_ASSERT_TRUE(isValidAddress(0x20U));
  TEST_ASSERT_TRUE(isValidAddress(0x27U));
  TEST_ASSERT_FALSE(isValidAddress(0x1FU));
  TEST_ASSERT_FALSE(isValidAddress(0x28U));

  ObservedState observed{};
  observed.validPairs = PAIR_OUTPUTS;
  TEST_ASSERT_TRUE(observed.valid(PAIR_OUTPUTS));
  TEST_ASSERT_FALSE(observed.valid(PAIR_ALL_WRITABLE));
  TEST_ASSERT_FALSE(observed.valid(PAIR_ALL));
  TEST_ASSERT_FALSE(observed.valid(PAIR_NONE));

  observed.validPairs = PAIR_ALL_WRITABLE;
  TEST_ASSERT_TRUE(observed.valid(PAIR_ALL_WRITABLE));
  TEST_ASSERT_FALSE(observed.valid(PAIR_ALL));
  TEST_ASSERT_FALSE(observed.valid(PAIR_NONE));

  observed.validPairs = PAIR_ALL;
  TEST_ASSERT_TRUE(observed.valid(PAIR_ALL_WRITABLE));
  TEST_ASSERT_TRUE(observed.valid(PAIR_ALL));
  TEST_ASSERT_FALSE(observed.valid(PAIR_NONE));
}

void test_bind_and_begin_are_passive_for_all_valid_addresses() {
  for (uint8_t address = 0x20U; address <= 0x27U; ++address) {
    FakeBus bus;
    bus.address = address;
    PCA9555::PCA9555 device;
    Config config = makeConfig(bus);
    config.i2cAddress = address;
    TEST_ASSERT_TRUE(device.bind(config).ok());
    TEST_ASSERT_TRUE(device.isBound());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                            static_cast<uint8_t>(device.state()));
    TEST_ASSERT_EQUAL_UINT32(0U, busTraffic(bus));

    PCA9555::PCA9555 alias;
    TEST_ASSERT_TRUE(alias.begin(config).ok());
    TEST_ASSERT_TRUE(alias.isBound());
    TEST_ASSERT_EQUAL_UINT32(0U, busTraffic(bus));
  }
}

void test_invalid_rebind_preserves_live_binding_without_io() {
  FakeBus first;
  first.address = 0x23U;
  PCA9555::PCA9555 device;
  Config good = makeConfig(first);
  good.i2cAddress = 0x23U;
  TEST_ASSERT_TRUE(device.bind(good).ok());

  FakeBus second;
  Config bad = makeConfig(second);
  bad.i2cWrite = nullptr;
  TEST_ASSERT_TRUE(device.bind(bad).is(Err::INVALID_CONFIG));
  TEST_ASSERT_TRUE(device.isBound());
  TEST_ASSERT_EQUAL_HEX8(0x23, device.getConfig().i2cAddress);
  TEST_ASSERT_TRUE(device.getConfig().i2cUser == &first);
  TEST_ASSERT_EQUAL_UINT32(0U, busTraffic(first));
  TEST_ASSERT_EQUAL_UINT32(0U, busTraffic(second));

  bad = makeConfig(second);
  bad.i2cAddress = 0x28U;
  TEST_ASSERT_TRUE(device.bind(bad).is(Err::INVALID_CONFIG));
  bad = makeConfig(second);
  bad.i2cTimeoutMs = 0U;
  TEST_ASSERT_TRUE(device.bind(bad).is(Err::INVALID_CONFIG));
  bad.i2cTimeoutMs = MAX_I2C_TIMEOUT_MS + 1U;
  TEST_ASSERT_TRUE(device.bind(bad).is(Err::INVALID_CONFIG));
  TEST_ASSERT_EQUAL_HEX8(0x23, device.getConfig().i2cAddress);
}

void test_probe_is_explicit_one_transfer_and_health_neutral() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.probe().ok());
  TEST_ASSERT_EQUAL_UINT32(1U, bus.readCalls);
  TEST_ASSERT_EQUAL_UINT32(0U, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0U, device.totalFailures());
  assertTransaction(bus, 0U, 'R', cmd::REG_CONFIG_PORT_0);

  bus.present = false;
  const uint32_t before = busTraffic(bus);
  const Status status = device.probe();
  TEST_ASSERT_TRUE(status.is(Err::DEVICE_NOT_FOUND));
  TEST_ASSERT_EQUAL_UINT32(before + 1U, busTraffic(bus));
  TEST_ASSERT_EQUAL_UINT32(0U, device.totalFailures());
}

void test_por_default_check_is_explicit_and_non_identity() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  PortData directions{};
  TEST_ASSERT_TRUE(device.checkPorDefaults(directions).ok());
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, directions.combined());
  TEST_ASSERT_EQUAL_UINT32(1U, bus.readCalls);

  bus.regs[cmd::REG_CONFIG_PORT_0] = 0xFEU;
  bus.regs[cmd::REG_CONFIG_PORT_1] = 0x7FU;
  const Status mismatch = device.checkPorDefaults(directions);
  TEST_ASSERT_TRUE(mismatch.is(Err::CONFIG_REG_MISMATCH));
  TEST_ASSERT_EQUAL_INT32(0x7FFE, mismatch.detail);
  TEST_ASSERT_EQUAL_HEX16(0x7FFE, directions.combined());
}

void test_apply_operation_budget_and_exactly_once_result() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  const RegisterImage expected = image();
  const uint32_t before = busTraffic(bus);
  TEST_ASSERT_TRUE(device.startApplyImage(41U, expected, 100U, 50U).inProgress());
  assertBusSilentSince(bus, before);

  uint8_t used = 99U;
  TEST_ASSERT_TRUE(device.pollOperation(41U, 100U, 0U, used).inProgress());
  TEST_ASSERT_EQUAL_UINT8(0U, used);
  assertBusSilentSince(bus, before);

  TEST_ASSERT_TRUE(device.pollOperation(41U, 101U, 1U, used).inProgress());
  TEST_ASSERT_EQUAL_UINT8(1U, used);
  TEST_ASSERT_EQUAL_UINT32(before + 1U, busTraffic(bus));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationPhase::APPLY_POLARITY),
                          static_cast<uint8_t>(device.operationPhase()));

  TEST_ASSERT_TRUE(device.pollOperation(41U, 102U, 8U, used).ok());
  TEST_ASSERT_EQUAL_UINT8(7U, used);
  TEST_ASSERT_FALSE(device.operationActive());
  TEST_ASSERT_TRUE(device.operationResultPending());
  TEST_ASSERT_EQUAL_UINT32(before + 8U, busTraffic(bus));
  assertTransaction(bus, 0U, 'W', cmd::REG_OUTPUT_PORT_0);
  assertTransaction(bus, 1U, 'W', cmd::REG_POLARITY_INV_0);
  assertTransaction(bus, 2U, 'W', cmd::REG_CONFIG_PORT_0);

  TEST_ASSERT_TRUE(device.startVerifyImage(42U, expected, 103U, 50U).is(Err::BUSY));
  OperationResult wrong;
  TEST_ASSERT_TRUE(device.takeOperationResult(42U, wrong).is(Err::BUSY));
  const OperationResult result = takeResult(device, 41U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationKind::APPLY_IMAGE),
                          static_cast<uint8_t>(result.kind));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_UINT8(8U, result.transactionsUsed);
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL, result.completedPairs);
  TEST_ASSERT_TRUE(device.takeOperationResult(41U, wrong).is(Err::NO_RESULT));
}

void test_request_identity_rejects_zero_wrong_and_overlapping_ids_bus_silently() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  const uint32_t before = busTraffic(bus);
  TEST_ASSERT_TRUE(device.startApplyImage(0U, image(), 0U, 10U).is(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(device.startApplyImage(1001U, image(), 0U, 10U).inProgress());
  TEST_ASSERT_TRUE(device.startVerifyImage(1002U, image(), 0U, 10U).is(Err::BUSY));
  uint8_t used = 9U;
  Status status = device.pollOperation(1002U, 0U, 1U, used);
  TEST_ASSERT_TRUE(status.is(Err::BUSY));
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(BusyDetail::REQUEST_ID_MISMATCH),
                          status.detail);
  TEST_ASSERT_EQUAL_UINT8(0U, used);
  TEST_ASSERT_TRUE(device.cancelOperation(1002U).is(Err::BUSY));
  TEST_ASSERT_TRUE(device.timeoutOperation(1002U).is(Err::BUSY));
  assertBusSilentSince(bus, before);

  TEST_ASSERT_TRUE(device.cancelOperation(1001U).ok());
  const OperationResult cancelled = takeResult(device, 1001U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                          static_cast<uint8_t>(cancelled.outcome));
  TEST_ASSERT_TRUE(device.startVerifyImage(1002U, image(), 1U, 10U).inProgress());
  TEST_ASSERT_TRUE(device.cancelOperation(1002U).ok());
  TEST_ASSERT_EQUAL_UINT32(1002U, takeResult(device, 1002U).requestId);
  OperationResult stale;
  TEST_ASSERT_TRUE(device.takeOperationResult(1001U, stale).is(Err::NO_RESULT));
}

void test_deadline_is_exact_wrap_safe_and_bus_silent() {
  {
    FakeBus bus;
    PCA9555::PCA9555 device;
    TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
    TEST_ASSERT_TRUE(device.startVerifyImage(51U, image(), 100U, 10U).inProgress());
    uint8_t used = 7U;
    const Status status = device.pollOperation(51U, 110U, 0U, used);
    TEST_ASSERT_TRUE(status.is(Err::TIMEOUT));
    TEST_ASSERT_EQUAL_UINT8(0U, used);
    TEST_ASSERT_EQUAL_UINT32(0U, busTraffic(bus));
    const OperationResult result = takeResult(device, 51U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::TIMED_OUT),
                            static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_EQUAL_UINT8(0U, result.transactionsUsed);
  }

  {
    FakeBus bus;
    PCA9555::PCA9555 device;
    TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
    const uint32_t start = UINT32_MAX - 15U;
    TEST_ASSERT_TRUE(device.startVerifyImage(52U, image(), start, 32U).inProgress());
    uint8_t used = 7U;
    TEST_ASSERT_TRUE(device.pollOperation(52U, UINT32_MAX, 0U, used).inProgress());
    TEST_ASSERT_EQUAL_UINT8(0U, used);
    TEST_ASSERT_TRUE(device.pollOperation(52U, 15U, 0U, used).inProgress());
    TEST_ASSERT_EQUAL_UINT8(0U, used);
    TEST_ASSERT_TRUE(device.pollOperation(52U, 16U, 0U, used).is(Err::TIMEOUT));
    TEST_ASSERT_EQUAL_UINT8(0U, used);
    TEST_ASSERT_EQUAL_UINT32(0U, busTraffic(bus));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::TIMED_OUT),
                            static_cast<uint8_t>(takeResult(device, 52U).outcome));
  }

  {
    FakeBus bus;
    PCA9555::PCA9555 device;
    TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
    TEST_ASSERT_TRUE(device.startVerifyImage(
        53U, image(), 1U, static_cast<uint32_t>(INT32_MAX) + 1U).is(Err::INVALID_PARAM));
    TEST_ASSERT_TRUE(device.startVerifyImage(
        53U, image(), 1U, static_cast<uint32_t>(INT32_MAX)).inProgress());
    TEST_ASSERT_TRUE(device.cancelOperation(53U).ok());
    (void)takeResult(device, 53U);
  }
}

void test_input_cancel_runs_required_pointer_park_before_terminal_result() {
  FakeBus bus;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0x12U;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0x34U;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.startReadInputs(61U, 100U, 50U).inProgress());

  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(61U, 100U, 1U, used).inProgress());
  TEST_ASSERT_EQUAL_UINT8(1U, used);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationPhase::POINTER_PARK),
                          static_cast<uint8_t>(device.operationPhase()));
  TEST_ASSERT_TRUE(device.cancelOperation(61U).inProgress());
  TEST_ASSERT_TRUE(device.operationActive());

  TEST_ASSERT_TRUE(device.pollOperation(61U, 101U, 0U, used).inProgress());
  TEST_ASSERT_EQUAL_UINT8(0U, used);
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);

  const Status terminal = device.pollOperation(61U, 102U, 1U, used);
  TEST_ASSERT_TRUE(terminal.is(Err::CANCELLED));
  TEST_ASSERT_EQUAL_UINT8(1U, used);
  TEST_ASSERT_EQUAL_UINT(2U, bus.transactionCount);
  assertTransaction(bus, 0U, 'R', cmd::REG_INPUT_PORT_0);
  assertTransaction(bus, 1U, 'W', cmd::ERRATA_SAFE_CMD);

  const OperationResult result = takeResult(device, 61U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_FALSE(result.cleanupRequired);
  TEST_ASSERT_TRUE(result.cleanupAttempted);
  TEST_ASSERT_TRUE(result.cleanupStatus.ok());
  TEST_ASSERT_TRUE(result.observed.valid(PAIR_INPUTS));
  TEST_ASSERT_EQUAL_HEX16(0x3412, result.observed.inputs);
}

void test_input_timeout_runs_pointer_park_and_preserves_timeout_cause() {
  FakeBus bus;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0xA5U;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0x5AU;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.startReadInputs(62U, 100U, 2U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(62U, 100U, 1U, used).inProgress());
  TEST_ASSERT_EQUAL_UINT8(1U, used);
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationPhase::POINTER_PARK),
                          static_cast<uint8_t>(device.operationPhase()));

  // Deadline processing is CPU-only. With no transfer budget the timeout is
  // latched, but owed pointer cleanup keeps the operation active and bus-silent.
  const Status latched = device.pollOperation(62U, 102U, 0U, used);
  TEST_ASSERT_TRUE(latched.inProgress());
  TEST_ASSERT_EQUAL_UINT8(0U, used);
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  TEST_ASSERT_TRUE(device.operationActive());
  TEST_ASSERT_FALSE(device.operationResultPending());

  const Status status = device.pollOperation(62U, 102U, 1U, used);
  TEST_ASSERT_TRUE(status.is(Err::TIMEOUT));
  TEST_ASSERT_EQUAL_UINT8(1U, used);
  TEST_ASSERT_EQUAL_UINT(2U, bus.transactionCount);
  assertTransaction(bus, 1U, 'W', cmd::ERRATA_SAFE_CMD);
  const OperationResult result = takeResult(device, 62U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::TIMED_OUT),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_UINT8(2U, result.transactionsUsed);
  TEST_ASSERT_TRUE(result.cleanupAfterDeadline);
  TEST_ASSERT_TRUE(result.cleanupAttempted);
  TEST_ASSERT_EQUAL_HEX16(0x5AA5, result.observed.inputs);
}

void test_cancel_cause_survives_deadline_during_required_pointer_cleanup() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.startReadInputs(621U, 100U, 2U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(621U, 100U, 1U, used).inProgress());
  TEST_ASSERT_TRUE(device.cancelOperation(621U).inProgress());
  const Status terminal = device.pollOperation(621U, 102U, 1U, used);
  TEST_ASSERT_TRUE(terminal.is(Err::CANCELLED));
  TEST_ASSERT_EQUAL_UINT8(1U, used);
  TEST_ASSERT_EQUAL_UINT(2U, bus.transactionCount);
  const OperationResult result = takeResult(device, 621U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_TRUE(result.status.is(Err::CANCELLED));
  TEST_ASSERT_TRUE(result.cleanupAfterDeadline);
  TEST_ASSERT_TRUE(result.cleanupAttempted);
  TEST_ASSERT_TRUE(result.cleanupStatus.ok());
}

void test_input_pointer_park_failure_keeps_valid_input_evidence() {
  FakeBus bus;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0x0FU;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0xF0U;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  armWriteFault(bus, cmd::ERRATA_SAFE_CMD, TransportCode::BUS_ERROR,
                WriteEffect::NOT_ATTEMPTED, 0U, -301);
  TEST_ASSERT_TRUE(device.startReadInputs(63U, 100U, 20U).inProgress());
  uint8_t used = 0U;
  const Status status = device.pollOperation(63U, 100U, 2U, used);
  TEST_ASSERT_TRUE(status.is(Err::I2C_BUS));
  TEST_ASSERT_EQUAL_UINT8(2U, used);
  const OperationResult result = takeResult(device, 63U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::FAILED),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationPhase::POINTER_PARK),
                          static_cast<uint8_t>(result.terminalPhase));
  TEST_ASSERT_TRUE(result.observed.valid(PAIR_INPUTS));
  TEST_ASSERT_EQUAL_HEX16(0xF00F, result.observed.inputs);
  TEST_ASSERT_TRUE(result.cleanupRequired);
  TEST_ASSERT_TRUE(result.cleanupAttempted);
  TEST_ASSERT_TRUE(result.cleanupStatus.is(Err::I2C_BUS));
}

void test_active_operation_blocks_synchronous_i2c_and_pointer_park_interleaving() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.startReadInputs(631U, 100U, 50U).inProgress());

  PortData values;
  TEST_ASSERT_TRUE(device.readOutputs(values).is(Err::BUSY));
  TEST_ASSERT_TRUE(device.probe().is(Err::BUSY));
  TEST_ASSERT_EQUAL_UINT(0U, bus.transactionCount);

  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(631U, 100U, 1U, used).inProgress());
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationPhase::POINTER_PARK),
                          static_cast<uint8_t>(device.operationPhase()));
  TEST_ASSERT_TRUE(device.writeOutputs(PortData::fromCombined(0U)).is(Err::BUSY));
  TEST_ASSERT_TRUE(device.readOutputs(values).is(Err::BUSY));
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);

  TEST_ASSERT_TRUE(device.pollOperation(631U, 101U, 1U, used).ok());
  (void)takeResult(device, 631U);
}

void test_cancel_before_first_transfer_is_immediate_and_bus_silent() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.startApplyImage(64U, image(), 0U, 10U).inProgress());
  TEST_ASSERT_TRUE(device.cancelOperation(64U).ok());
  TEST_ASSERT_EQUAL_UINT32(0U, busTraffic(bus));
  const OperationResult result = takeResult(device, 64U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_UINT8(0U, result.transactionsUsed);
}

void test_cancel_and_explicit_timeout_after_apply_phase_preserve_partial_evidence() {
  for (uint8_t mode = 0U; mode < 2U; ++mode) {
    FakeBus bus;
    PCA9555::PCA9555 device;
    TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
    const uint32_t requestId = static_cast<uint32_t>(65U + mode);
    TEST_ASSERT_TRUE(device.startApplyImage(requestId, image(), 0U, 100U).inProgress());
    uint8_t used = 0U;
    TEST_ASSERT_TRUE(device.pollOperation(requestId, 0U, 1U, used).inProgress());
    TEST_ASSERT_EQUAL_UINT8(1U, used);
    const uint32_t before = busTraffic(bus);
    const Status terminal = mode == 0U
        ? device.cancelOperation(requestId)
        : device.timeoutOperation(requestId);
    TEST_ASSERT_TRUE(terminal.ok());
    assertBusSilentSince(bus, before);
    const OperationResult result = takeResult(device, requestId);
    TEST_ASSERT_EQUAL_HEX8(PAIR_OUTPUTS, result.completedPairs);
    TEST_ASSERT_EQUAL_UINT8(1U, result.transactionsUsed);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(mode == 0U ? OperationOutcome::CANCELLED
                                        : OperationOutcome::TIMED_OUT),
        static_cast<uint8_t>(result.outcome));
  }
}

void test_apply_reports_failure_at_every_phase_without_hidden_retry() {
  static constexpr char TYPES[] = {
      'W', 'W', 'W', 'R', 'R', 'R', 'R', 'W'};
  static constexpr uint8_t REGS[] = {
      cmd::REG_OUTPUT_PORT_0,
      cmd::REG_POLARITY_INV_0,
      cmd::REG_CONFIG_PORT_0,
      cmd::REG_OUTPUT_PORT_0,
      cmd::REG_POLARITY_INV_0,
      cmd::REG_CONFIG_PORT_0,
      cmd::REG_INPUT_PORT_0,
      cmd::ERRATA_SAFE_CMD};
  static constexpr OperationPhase PHASES[] = {
      OperationPhase::APPLY_OUTPUTS,
      OperationPhase::APPLY_POLARITY,
      OperationPhase::APPLY_DIRECTIONS,
      OperationPhase::VERIFY_OUTPUTS,
      OperationPhase::VERIFY_POLARITY,
      OperationPhase::VERIFY_DIRECTIONS,
      OperationPhase::READ_INPUTS,
      OperationPhase::POINTER_PARK};
  static constexpr uint8_t COMPLETED_BEFORE_FAILURE[] = {
      PAIR_NONE,
      PAIR_OUTPUTS,
      static_cast<uint8_t>(PAIR_OUTPUTS | PAIR_POLARITY),
      PAIR_ALL_WRITABLE,
      PAIR_ALL_WRITABLE,
      PAIR_ALL_WRITABLE,
      PAIR_ALL_WRITABLE,
      PAIR_ALL};

  for (size_t failedPhase = 0U; failedPhase < 8U; ++failedPhase) {
    FakeBus bus;
    PCA9555::PCA9555 device;
    TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
    if (TYPES[failedPhase] == 'W') {
      armWriteFault(bus, REGS[failedPhase], TransportCode::BUS_ERROR,
                    WriteEffect::NOT_ATTEMPTED, 0U,
                    static_cast<int32_t>(-310 - failedPhase));
      if (PHASES[failedPhase] == OperationPhase::POINTER_PARK) {
        bus.fault.txLen = 1U;
      }
    } else {
      armReadFault(bus, REGS[failedPhase], TransportCode::BUS_ERROR, 0U,
                   static_cast<int32_t>(-310 - failedPhase));
    }
    TEST_ASSERT_TRUE(device.startApplyImage(70U, image(), 10U, 100U).inProgress());
    uint8_t used = 0U;
    const Status status = device.pollOperation(70U, 10U, 10U, used);
    TEST_ASSERT_TRUE(status.is(Err::I2C_BUS));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(failedPhase + 1U), used);
    TEST_ASSERT_EQUAL_UINT(failedPhase + 1U, bus.transactionCount);
    for (size_t transfer = 0U; transfer <= failedPhase; ++transfer) {
      assertTransaction(bus, transfer, TYPES[transfer], REGS[transfer]);
    }
    const OperationResult result = takeResult(device, 70U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::FAILED),
                            static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PHASES[failedPhase]),
                            static_cast<uint8_t>(result.terminalPhase));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(failedPhase + 1U),
                            result.transactionsUsed);
    TEST_ASSERT_EQUAL_HEX8(COMPLETED_BEFORE_FAILURE[failedPhase],
                           result.completedPairs);
    TEST_ASSERT_EQUAL(failedPhase == 7U, result.cleanupAttempted);
  }
}

void test_verify_reports_failure_at_every_phase_and_retains_completed_pairs() {
  static constexpr uint8_t REGS[] = {
      cmd::REG_OUTPUT_PORT_0,
      cmd::REG_POLARITY_INV_0,
      cmd::REG_CONFIG_PORT_0};
  static constexpr uint8_t PAIRS[] = {
      PAIR_OUTPUTS,
      PAIR_POLARITY,
      PAIR_DIRECTIONS};
  static constexpr OperationPhase PHASES[] = {
      OperationPhase::VERIFY_OUTPUTS,
      OperationPhase::VERIFY_POLARITY,
      OperationPhase::VERIFY_DIRECTIONS};

  for (size_t failedPhase = 0U; failedPhase < 3U; ++failedPhase) {
    FakeBus bus;
    PCA9555::PCA9555 device;
    bindAndApply(device, bus);
    clearTransactions(bus);
    armReadFault(bus, REGS[failedPhase], TransportCode::TIMEOUT, 0U,
                 static_cast<int32_t>(-320 - failedPhase));
    TEST_ASSERT_TRUE(device.startVerifyImage(71U, image(), 20U, 100U).inProgress());
    uint8_t used = 0U;
    const Status status = device.pollOperation(71U, 20U, 10U, used);
    TEST_ASSERT_TRUE(status.is(Err::I2C_TIMEOUT));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(failedPhase + 1U), used);
    const OperationResult result = takeResult(device, 71U);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::FAILED),
                            static_cast<uint8_t>(result.outcome));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PHASES[failedPhase]),
                            static_cast<uint8_t>(result.terminalPhase));
    uint8_t expectedValid = PAIR_NONE;
    for (size_t i = 0U; i < failedPhase; ++i) expectedValid |= PAIRS[i];
    TEST_ASSERT_EQUAL_HEX8(expectedValid, result.observed.validPairs);
    TEST_ASSERT_EQUAL_HEX8(expectedValid, result.completedPairs);
  }

  FakeBus successBus;
  PCA9555::PCA9555 successDevice;
  const RegisterImage expected = image();
  bindAndApply(successDevice, successBus, expected);
  clearTransactions(successBus);
  TEST_ASSERT_TRUE(
      successDevice.startVerifyImage(711U, expected, 21U, 100U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(successDevice.pollOperation(711U, 21U, 3U, used).ok());
  const OperationResult success = takeResult(successDevice, 711U);
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, success.completedPairs);
}

void test_read_observed_state_failure_returns_only_current_partial_evidence() {
  static constexpr uint8_t REGS[] = {
      cmd::REG_OUTPUT_PORT_0,
      cmd::REG_POLARITY_INV_0,
      cmd::REG_CONFIG_PORT_0};
  static constexpr uint8_t PAIRS[] = {
      PAIR_OUTPUTS,
      PAIR_POLARITY,
      PAIR_DIRECTIONS};

  for (size_t failedRead = 0U; failedRead < 3U; ++failedRead) {
    FakeBus bus;
    PCA9555::PCA9555 device;
    bindAndApply(device, bus);

    // Seed a complete prior observation, then make every writable pair
    // uncertain so successful diagnostic reads must report partial evidence.
    ObservedState prior;
    TEST_ASSERT_TRUE(device.readObservedState(prior).ok());
    TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, prior.validPairs);

    armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::TIMEOUT,
                  WriteEffect::MAY_HAVE_COMMITTED, 0U, -325);
    TEST_ASSERT_TRUE(
        device.writeOutputs(PortData::fromCombined(0x1357U)).is(Err::I2C_TIMEOUT));
    armWriteFault(bus, cmd::REG_POLARITY_INV_0, TransportCode::TIMEOUT,
                  WriteEffect::MAY_HAVE_COMMITTED, 0U, -326);
    TEST_ASSERT_TRUE(
        device.setPolarity(PortData::fromCombined(0x2468U)).is(Err::I2C_TIMEOUT));
    armWriteFault(bus, cmd::REG_CONFIG_PORT_0, TransportCode::TIMEOUT,
                  WriteEffect::MAY_HAVE_COMMITTED, 0U, -327);
    TEST_ASSERT_TRUE(
        device.setConfiguration(PortData{0xFFU, 0xFFU}).is(Err::I2C_TIMEOUT));
    TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, device.uncertainPairs());

    clearTransactions(bus);
    const int32_t detail = static_cast<int32_t>(-328 - failedRead);
    armReadFault(bus, REGS[failedRead], TransportCode::BUS_ERROR, 0U, detail);
    ObservedState observed{};
    observed.validPairs = PAIR_ALL;
    observed.mismatchPairs = PAIR_ALL;
    observed.uncertainPairs = PAIR_ALL;

    const Status status = device.readObservedState(observed);
    TEST_ASSERT_TRUE(status.is(Err::I2C_BUS));
    TEST_ASSERT_EQUAL_INT32(detail, status.detail);
    TEST_ASSERT_EQUAL_UINT(failedRead + 1U, bus.transactionCount);
    for (size_t read = 0U; read <= failedRead; ++read) {
      assertTransaction(bus, read, 'R', REGS[read]);
    }

    uint8_t expectedValid = PAIR_NONE;
    for (size_t read = 0U; read < failedRead; ++read) {
      expectedValid = static_cast<uint8_t>(expectedValid | PAIRS[read]);
    }
    TEST_ASSERT_EQUAL_HEX8(expectedValid, observed.validPairs);
    TEST_ASSERT_EQUAL_HEX8(expectedValid, observed.uncertainPairs);
    TEST_ASSERT_EQUAL_HEX8(
        PAIR_NONE,
        static_cast<uint8_t>(observed.uncertainPairs & ~observed.validPairs));
  }
}

void test_verify_reports_exact_pair_mismatches_without_changing_expected_state() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  const RegisterImage expected = image(0x1234U, 0x5678U, 0x9ABCU);
  bindAndApply(device, bus, expected);
  bus.regs[cmd::REG_OUTPUT_PORT_1] ^= 0x80U;
  bus.regs[cmd::REG_POLARITY_INV_0] ^= 0x04U;
  bus.regs[cmd::REG_CONFIG_PORT_0] ^= 0x01U;
  clearTransactions(bus);

  TEST_ASSERT_TRUE(device.startVerifyImage(72U, expected, 30U, 100U).inProgress());
  uint8_t used = 0U;
  const Status status = device.pollOperation(72U, 30U, 3U, used);
  TEST_ASSERT_TRUE(status.is(Err::VERIFY_MISMATCH));
  TEST_ASSERT_EQUAL_UINT8(3U, used);
  const OperationResult result = takeResult(device, 72U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::MISMATCH),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, result.observed.validPairs);
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, result.mismatchPairs);
  TEST_ASSERT_EQUAL_HEX16(
      static_cast<uint16_t>(expected.outputs ^ 0x8000U),
      result.observed.registers.outputs);
  TEST_ASSERT_EQUAL_HEX16(
      static_cast<uint16_t>(expected.directions ^ 0x0001U),
      result.observed.registers.directions);

  // Definite mismatches invalidate RMW eligibility. Further reads may update
  // observations but must not silently adopt hardware state as write intent.
  ObservedState reread;
  TEST_ASSERT_TRUE(device.readObservedState(reread).ok());
  clearTransactions(bus);
  Status outputRmw = device.writePin(Pin::P00, Level::HIGH_LEVEL);
  Status polarityRmw = device.setPinPolarity(Pin::P00, true);
  Status directionRmw = device.configureInputBits(0x0001U);
  TEST_ASSERT_TRUE(outputRmw.is(Err::SHADOW_INVALID) ||
                   outputRmw.is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(polarityRmw.is(Err::SHADOW_INVALID) ||
                   polarityRmw.is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(directionRmw.is(Err::SHADOW_INVALID) ||
                   directionRmw.is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_EQUAL_UINT(0U, bus.transactionCount);
}

void test_ambiguous_output_write_is_terminal_and_never_replayed() {
  static constexpr size_t APPLIED_COUNTS[] = {0U, 1U, 2U};
  for (size_t applied : APPLIED_COUNTS) {
    FakeBus bus;
    PCA9555::PCA9555 device;
    bindAndApply(device, bus, RegisterImage{});
    clearTransactions(bus);
    armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::TIMEOUT,
                  WriteEffect::MAY_HAVE_COMMITTED, applied, -330);
    const Status status = device.writeOutputs(PortData::fromCombined(0x1234U));
    TEST_ASSERT_TRUE(status.is(Err::I2C_TIMEOUT));
    TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
    TEST_ASSERT_EQUAL_UINT(applied, bus.transactions[0].dataBytesApplied);
    TEST_ASSERT_TRUE((device.uncertainPairs() & PAIR_OUTPUTS) != 0U);
    TEST_ASSERT_TRUE((device.shadowValidPairs() & PAIR_OUTPUTS) == 0U);
    TEST_ASSERT_EQUAL_UINT32(1U, device.totalFailures());
  }
}

void test_ambiguous_apply_never_advances_to_unsafe_later_phase() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::BUS_ERROR,
                WriteEffect::MAY_HAVE_COMMITTED, 1U, -331);
  TEST_ASSERT_TRUE(device.startApplyImage(73U, image(), 0U, 100U).inProgress());
  uint8_t used = 0U;
  const Status status = device.pollOperation(73U, 0U, 8U, used);
  TEST_ASSERT_TRUE(status.is(Err::I2C_BUS));
  TEST_ASSERT_EQUAL_UINT8(1U, used);
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  const OperationResult result = takeResult(device, 73U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::INDETERMINATE),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationPhase::APPLY_OUTPUTS),
                          static_cast<uint8_t>(result.terminalPhase));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteEffect::MAY_HAVE_COMMITTED),
                          static_cast<uint8_t>(result.lastWriteEffect));
  TEST_ASSERT_EQUAL_HEX8(PAIR_OUTPUTS, result.uncertainPairs);
}

void test_not_attempted_write_failure_is_definite_not_indeterminate() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  const uint8_t validBefore = device.shadowValidPairs();
  clearTransactions(bus);
  armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::NACK_ADDRESS,
                WriteEffect::NOT_ATTEMPTED, 0U, -332);
  const Status status = device.writeOutputs(PortData::fromCombined(0x0000U));
  TEST_ASSERT_TRUE(status.is(Err::I2C_NACK_ADDR));
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(validBefore, device.shadowValidPairs());
  TEST_ASSERT_EQUAL_HEX8(PAIR_NONE, device.uncertainPairs());
}

void test_non_ok_transport_cannot_claim_definite_committed_write() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::TIMEOUT,
                WriteEffect::COMMITTED, 2U, -334);
  TEST_ASSERT_TRUE(device.startApplyImage(741U, image(0x1234U), 10U, 100U)
                       .inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(741U, 10U, 1U, used).is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_UINT8(1U, used);
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  const OperationResult result = takeResult(device, 741U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::INDETERMINATE),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(WriteEffect::MAY_HAVE_COMMITTED),
                          static_cast<uint8_t>(result.lastWriteEffect));
  TEST_ASSERT_TRUE((result.uncertainPairs & PAIR_OUTPUTS) != 0U);
  TEST_ASSERT_TRUE((result.shadowValidPairs & PAIR_OUTPUTS) == 0U);
  const uint32_t before = busTraffic(bus);
  TEST_ASSERT_TRUE(device.writePin(Pin::P00, Level::HIGH_LEVEL)
                       .is(Err::STATE_UNCERTAIN));
  assertBusSilentSince(bus, before);
}

void test_matching_verify_reconciles_an_ambiguous_full_commit_without_reapply() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  const RegisterImage original{};
  bindAndApply(device, bus, original);
  const RegisterImage intended{0x1234U, 0U, 0xFFFFU};
  clearTransactions(bus);
  armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::TIMEOUT,
                WriteEffect::MAY_HAVE_COMMITTED, 2U, -333);
  TEST_ASSERT_TRUE(device.writeOutputs(PortData::fromCombined(intended.outputs))
                       .is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);

  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.startVerifyImage(74U, intended, 10U, 100U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(74U, 10U, 3U, used).ok());
  const OperationResult verified = takeResult(device, 74U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                          static_cast<uint8_t>(verified.outcome));
  TEST_ASSERT_EQUAL_UINT(3U, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(PAIR_NONE, device.uncertainPairs());
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, device.shadowValidPairs());
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, verified.completedPairs);

  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.togglePin(Pin::P00).ok());
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
}

void test_mixed_verify_reconciles_matches_and_keeps_mismatch_fenced() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});

  armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::TIMEOUT,
                WriteEffect::MAY_HAVE_COMMITTED, 2U, -335);
  TEST_ASSERT_TRUE(device.writeOutputs(PortData::fromCombined(0x1234U))
                       .is(Err::I2C_TIMEOUT));
  armWriteFault(bus, cmd::REG_POLARITY_INV_0, TransportCode::TIMEOUT,
                WriteEffect::MAY_HAVE_COMMITTED, 2U, -336);
  TEST_ASSERT_TRUE(device.setPolarity(PortData::fromCombined(0x00F0U))
                       .is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_HEX8(
      static_cast<uint8_t>(PAIR_OUTPUTS | PAIR_POLARITY),
      device.uncertainPairs());

  const RegisterImage expected{0x1234U, 0x0F0FU, 0xFFFFU};
  clearTransactions(bus);
  TEST_ASSERT_TRUE(
      device.startVerifyImage(742U, expected, 10U, 100U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(
      device.pollOperation(742U, 10U, 3U, used).is(Err::VERIFY_MISMATCH));
  const OperationResult result = takeResult(device, 742U);
  TEST_ASSERT_EQUAL_HEX8(PAIR_POLARITY, result.mismatchPairs);
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, result.completedPairs);
  TEST_ASSERT_EQUAL_HEX16(0x1234, result.observed.registers.outputs);
  TEST_ASSERT_EQUAL_HEX16(0x00F0, result.observed.registers.polarity);
  TEST_ASSERT_EQUAL_HEX8(PAIR_POLARITY, device.uncertainPairs());
  TEST_ASSERT_EQUAL_HEX8(
      static_cast<uint8_t>(PAIR_OUTPUTS | PAIR_DIRECTIONS),
      device.shadowValidPairs());

  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.togglePin(Pin::P00).ok());
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  const uint32_t beforePolarityRmw = busTraffic(bus);
  TEST_ASSERT_TRUE(device.setInvertBits(pinMask(Pin::P00))
                       .is(Err::STATE_UNCERTAIN));
  assertBusSilentSince(bus, beforePolarityRmw);
}

void test_invalid_output_shadow_fences_all_output_rmw_paths_without_io() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::TIMEOUT,
                WriteEffect::MAY_HAVE_COMMITTED, 1U, -340);
  TEST_ASSERT_TRUE(device.writeOutputs(PortData::fromCombined(0xFFFEU))
                       .is(Err::I2C_TIMEOUT));
  const uint32_t before = busTraffic(bus);

  TEST_ASSERT_TRUE(device.writePin(Pin::P00, Level::LOW_LEVEL).is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(device.preloadOutput(Pin::P00, Level::LOW_LEVEL).is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(device.preloadOutputs(0x0001U, 0U).is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(device.setOutputBits(0x0001U).is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(device.clearOutputBits(0x0001U).is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(device.toggleOutputBits(0x0001U).is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(device.togglePin(Pin::P00).is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_TRUE(device.configureOutputs(0x0001U, 0U).is(Err::STATE_UNCERTAIN));
  assertBusSilentSince(bus, before);
}

void test_invalid_direction_and_polarity_shadows_fence_relevant_rmw_paths() {
  {
    FakeBus bus;
    PCA9555::PCA9555 device;
    bindAndApply(device, bus, RegisterImage{0xFFFFU, 0U, 0x0000U});
    armWriteFault(bus, cmd::REG_CONFIG_PORT_0, TransportCode::BUS_ERROR,
                  WriteEffect::MAY_HAVE_COMMITTED, 1U, -341);
    TEST_ASSERT_TRUE(device.setConfiguration(PortData::fromCombined(0xFFFFU))
                         .is(Err::I2C_BUS));
    const uint32_t before = busTraffic(bus);
    TEST_ASSERT_TRUE(device.configureInputBits(0x0001U).is(Err::STATE_UNCERTAIN));
    TEST_ASSERT_TRUE(device.configureOutputBits(0x0001U).is(Err::STATE_UNCERTAIN));
    TEST_ASSERT_TRUE(device.setDirection(Pin::P00, Direction::INPUT_MODE)
                         .is(Err::STATE_UNCERTAIN));
    TEST_ASSERT_TRUE(device.setPinDirection(Pin::P00, true)
                         .is(Err::STATE_UNCERTAIN));
    assertBusSilentSince(bus, before);
  }

  {
    FakeBus bus;
    PCA9555::PCA9555 device;
    bindAndApply(device, bus, RegisterImage{});
    armWriteFault(bus, cmd::REG_POLARITY_INV_0, TransportCode::TIMEOUT,
                  WriteEffect::MAY_HAVE_COMMITTED, 1U, -342);
    TEST_ASSERT_TRUE(device.setPolarity(PortData::fromCombined(0x0101U))
                         .is(Err::I2C_TIMEOUT));
    const uint32_t before = busTraffic(bus);
    TEST_ASSERT_TRUE(device.setPinPolarity(Pin::P00, true).is(Err::STATE_UNCERTAIN));
    TEST_ASSERT_TRUE(device.setInvertBits(0x0001U).is(Err::STATE_UNCERTAIN));
    TEST_ASSERT_TRUE(device.clearInvertBits(0x0001U).is(Err::STATE_UNCERTAIN));
    assertBusSilentSince(bus, before);
  }
}

void test_successful_read_after_por_updates_observed_not_write_shadow() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  const RegisterImage expected{0xA55AU, 0x1234U, 0x0FF0U};
  bindAndApply(device, bus, expected);
  fakePowerCycle(bus);
  clearTransactions(bus);

  ObservedState observed;
  TEST_ASSERT_TRUE(device.readObservedState(observed).ok());
  TEST_ASSERT_EQUAL_UINT(3U, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE,
                         static_cast<uint8_t>(observed.validPairs & PAIR_ALL_WRITABLE));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, observed.registers.outputs);
  TEST_ASSERT_EQUAL_HEX16(0x0000, observed.registers.polarity);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, observed.registers.directions);

  clearTransactions(bus);
  const Status blocked = device.writePin(Pin::P00, Level::HIGH_LEVEL);
  TEST_ASSERT_TRUE(blocked.is(Err::SHADOW_INVALID) ||
                   blocked.is(Err::STATE_UNCERTAIN));
  TEST_ASSERT_EQUAL_UINT(0U, bus.transactionCount);

  const OperationResult reconciled = applyImage(device, bus, expected, 76U, 200U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                          static_cast<uint8_t>(reconciled.outcome));
  TEST_ASSERT_EQUAL_HEX16(expected.outputs,
      static_cast<uint16_t>(bus.regs[cmd::REG_OUTPUT_PORT_0] |
          (static_cast<uint16_t>(bus.regs[cmd::REG_OUTPUT_PORT_1]) << 8U)));
}

void test_ordinary_pair_reads_fence_only_the_externally_changed_shadow_pair() {
  static constexpr uint8_t PAIRS[] = {
      PAIR_OUTPUTS,
      PAIR_POLARITY,
      PAIR_DIRECTIONS};
  static constexpr uint16_t POR_VALUES[] = {0xFFFFU, 0x0000U, 0xFFFFU};

  for (size_t index = 0U; index < 3U; ++index) {
    FakeBus bus;
    PCA9555::PCA9555 device;
    bindAndApply(device, bus, image());
    fakePowerCycle(bus);
    clearTransactions(bus);

    PortData value{};
    Status status = Status::Error(Err::INVALID_PARAM, "read not attempted");
    if (PAIRS[index] == PAIR_OUTPUTS) status = device.readOutputs(value);
    if (PAIRS[index] == PAIR_POLARITY) status = device.getPolarity(value);
    if (PAIRS[index] == PAIR_DIRECTIONS) status = device.getConfiguration(value);
    TEST_ASSERT_TRUE(status.ok());
    TEST_ASSERT_EQUAL_HEX16(POR_VALUES[index], value.combined());
    TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);

    const ObservedState observed = device.lastObservedState();
    TEST_ASSERT_TRUE(observed.valid(static_cast<StatePair>(PAIRS[index])));
    uint16_t observedValue = observed.registers.outputs;
    if (PAIRS[index] == PAIR_POLARITY) observedValue = observed.registers.polarity;
    if (PAIRS[index] == PAIR_DIRECTIONS) observedValue = observed.registers.directions;
    TEST_ASSERT_EQUAL_HEX16(POR_VALUES[index], observedValue);
    TEST_ASSERT_EQUAL_HEX8(allWritableExcept(PAIRS[index]),
                           device.shadowValidPairs());

    const uint32_t beforeRmw = busTraffic(bus);
    TEST_ASSERT_TRUE(attemptCachedRmw(device, PAIRS[index]).is(Err::SHADOW_INVALID));
    assertBusSilentSince(bus, beforeRmw);
  }
}

void test_named_and_raw_reads_fence_the_whole_pair_on_any_observed_mismatch() {
  static constexpr uint8_t REGS[] = {
      cmd::REG_OUTPUT_PORT_0,
      cmd::REG_POLARITY_INV_0,
      cmd::REG_CONFIG_PORT_0};
  static constexpr uint8_t PAIRS[] = {
      PAIR_OUTPUTS,
      PAIR_POLARITY,
      PAIR_DIRECTIONS};

  // Read modes: named one-byte API, raw one-byte API, raw full-pair API.
  for (uint8_t mode = 0U; mode < 3U; ++mode) {
    for (size_t index = 0U; index < 3U; ++index) {
      FakeBus bus;
      PCA9555::PCA9555 device;
      bindAndApply(device, bus, image());
      bus.regs[REGS[index]] ^= 0x01U;
      clearTransactions(bus);

      uint8_t values[2] = {0U, 0U};
      Status status = Status::Error(Err::INVALID_PARAM, "read not attempted");
      if (mode == 0U && PAIRS[index] == PAIR_OUTPUTS) {
        status = device.readOutput(Port::PORT_0, values[0]);
      } else if (mode == 0U && PAIRS[index] == PAIR_POLARITY) {
        status = device.getPortPolarity(Port::PORT_0, values[0]);
      } else if (mode == 0U) {
        status = device.getPortConfiguration(Port::PORT_0, values[0]);
      } else if (mode == 1U) {
        status = device.readRegister(REGS[index], values[0]);
      } else {
        status = device.readRegisters(REGS[index], values, 2U);
      }
      TEST_ASSERT_TRUE(status.ok());
      TEST_ASSERT_EQUAL_HEX8(bus.regs[REGS[index]], values[0]);
      TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
      TEST_ASSERT_EQUAL_HEX8(allWritableExcept(PAIRS[index]),
                             device.shadowValidPairs());
      TEST_ASSERT_EQUAL_HEX8(
          PAIRS[index],
          static_cast<uint8_t>(device.lastObservedState().mismatchPairs &
                               PAIRS[index]));

      const uint32_t beforeRmw = busTraffic(bus);
      TEST_ASSERT_TRUE(
          attemptCachedRmw(device, PAIRS[index]).is(Err::SHADOW_INVALID));
      assertBusSilentSince(bus, beforeRmw);
    }
  }
}

void test_verify_caller_mismatch_preserves_a_truthful_protocol_shadow() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  const RegisterImage hardware = image();
  bindAndApply(device, bus, hardware);
  RegisterImage callerExpected = hardware;
  callerExpected.outputs ^= pinMask(Pin::P00);
  clearTransactions(bus);

  TEST_ASSERT_TRUE(
      device.startVerifyImage(751U, callerExpected, 10U, 100U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(
      device.pollOperation(751U, 10U, 3U, used).is(Err::VERIFY_MISMATCH));
  TEST_ASSERT_EQUAL_UINT8(3U, used);
  const OperationResult result = takeResult(device, 751U);
  TEST_ASSERT_EQUAL_HEX8(PAIR_OUTPUTS, result.mismatchPairs);
  TEST_ASSERT_EQUAL_HEX16(hardware.outputs, result.observed.registers.outputs);
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, result.shadowValidPairs);
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, device.shadowValidPairs());

  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.togglePin(Pin::P00).ok());
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
}

void test_verify_matching_external_image_reconciles_observation_and_shadow() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  const RegisterImage original = image();
  bindAndApply(device, bus, original);
  RegisterImage external = original;
  external.outputs ^= pinMask(Pin::P00);
  bus.regs[cmd::REG_OUTPUT_PORT_0] =
      static_cast<uint8_t>(external.outputs & 0xFFU);
  bus.regs[cmd::REG_OUTPUT_PORT_1] =
      static_cast<uint8_t>((external.outputs >> 8U) & 0xFFU);
  clearTransactions(bus);

  TEST_ASSERT_TRUE(
      device.startVerifyImage(752U, external, 10U, 100U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(752U, 10U, 3U, used).ok());
  const OperationResult result = takeResult(device, 752U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::SUCCEEDED),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_EQUAL_HEX8(PAIR_NONE, result.mismatchPairs);
  TEST_ASSERT_EQUAL_HEX8(PAIR_NONE,
                         device.lastObservedState().mismatchPairs);
  TEST_ASSERT_EQUAL_HEX8(PAIR_ALL_WRITABLE, device.shadowValidPairs());
  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.togglePin(Pin::P01).ok());
  TEST_ASSERT_EQUAL_HEX16(
      static_cast<uint16_t>(external.outputs ^ pinMask(Pin::P01)),
      static_cast<uint16_t>(bus.regs[cmd::REG_OUTPUT_PORT_0] |
          (static_cast<uint16_t>(bus.regs[cmd::REG_OUTPUT_PORT_1]) << 8U)));
}

void test_repeated_failures_never_gate_owner_requested_io_or_retry_internally() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  const uint32_t failuresBefore = device.totalFailures();
  const uint32_t successesBefore = device.totalSuccess();

  for (uint8_t i = 0U; i < 8U; ++i) {
    armReadFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::BUS_ERROR, 0U,
                 static_cast<int32_t>(-350 - i));
    PortData outputs;
    TEST_ASSERT_TRUE(device.readOutputs(outputs).is(Err::I2C_BUS));
    TEST_ASSERT_EQUAL_UINT(i + 1U, bus.transactionCount);
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::DEGRADED),
                          static_cast<uint8_t>(device.state()));
  TEST_ASSERT_EQUAL_UINT32(failuresBefore + 8U, device.totalFailures());

  PortData outputs;
  TEST_ASSERT_TRUE(device.readOutputs(outputs).ok());
  TEST_ASSERT_EQUAL_UINT(9U, bus.transactionCount);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DriverState::READY),
                          static_cast<uint8_t>(device.state()));
  TEST_ASSERT_EQUAL_UINT8(0U, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(successesBefore + 1U, device.totalSuccess());
}

void test_safe_direction_change_writes_latch_before_direction() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.configureOutputs(0x0101U, 0x0100U).ok());
  TEST_ASSERT_EQUAL_UINT(2U, bus.transactionCount);
  assertTransaction(bus, 0U, 'W', cmd::REG_OUTPUT_PORT_0);
  assertTransaction(bus, 1U, 'W', cmd::REG_CONFIG_PORT_0);
  TEST_ASSERT_EQUAL_HEX8(0xFE, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_OUTPUT_PORT_1]);
  TEST_ASSERT_EQUAL_HEX8(0xFE, bus.regs[cmd::REG_CONFIG_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0xFE, bus.regs[cmd::REG_CONFIG_PORT_1]);
}

void test_typed_port_pin_direction_and_polarity_round_trip() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});

  TEST_ASSERT_TRUE(device.writeOutput(Port::PORT_0, 0xAAU).ok());
  TEST_ASSERT_TRUE(device.writeOutput(Port::PORT_1, 0x55U).ok());
  PortData outputs;
  TEST_ASSERT_TRUE(device.readOutputs(outputs).ok());
  TEST_ASSERT_EQUAL_HEX16(0x55AA, outputs.combined());
  Level outputLevel = Level::LOW_LEVEL;
  TEST_ASSERT_TRUE(device.readOutputPin(Pin::P01, outputLevel).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Level::HIGH_LEVEL),
                          static_cast<uint8_t>(outputLevel));

  TEST_ASSERT_TRUE(device.setPolarity(PortData::fromCombined(0x0101U)).ok());
  bool inverted = false;
  TEST_ASSERT_TRUE(device.getPinPolarity(Pin::P00, inverted).ok());
  TEST_ASSERT_TRUE(inverted);
  TEST_ASSERT_TRUE(device.setPinPolarity(Pin::P00, false).ok());

  TEST_ASSERT_TRUE(device.setDirection(Pin::P00, Direction::OUTPUT_MODE).ok());
  Direction direction = Direction::INPUT_MODE;
  TEST_ASSERT_TRUE(device.getPinDirection(Pin::P00, direction).ok());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Direction::OUTPUT_MODE),
                          static_cast<uint8_t>(direction));
}

void test_failed_safe_preload_never_advances_direction() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  armWriteFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::TIMEOUT,
                WriteEffect::NOT_ATTEMPTED, 0U, -351);
  TEST_ASSERT_TRUE(device.configureOutputs(0x0001U, 0U).is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_CONFIG_PORT_0]);
}

void test_failed_direction_after_preload_retains_partial_and_uncertain_evidence() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  armWriteFault(bus, cmd::REG_CONFIG_PORT_0, TransportCode::BUS_ERROR,
                WriteEffect::MAY_HAVE_COMMITTED, 1U, -352);
  TEST_ASSERT_TRUE(device.configureOutputs(0x0001U, 0U).is(Err::I2C_BUS));
  TEST_ASSERT_EQUAL_UINT(2U, bus.transactionCount);
  assertTransaction(bus, 0U, 'W', cmd::REG_OUTPUT_PORT_0);
  assertTransaction(bus, 1U, 'W', cmd::REG_CONFIG_PORT_0);
  TEST_ASSERT_EQUAL_HEX8(0xFE, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_TRUE((device.uncertainPairs() & PAIR_DIRECTIONS) != 0U);
}

void test_raw_configuration_writes_are_rejected_without_io() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  const uint8_t data[2] = {0U, 0U};
  TEST_ASSERT_TRUE(device.writeRegister(cmd::REG_CONFIG_PORT_0, 0U)
                       .is(Err::UNSUPPORTED));
  TEST_ASSERT_TRUE(device.writeRegisters(cmd::REG_CONFIG_PORT_0, data, 2U)
                       .is(Err::UNSUPPORTED));
  TEST_ASSERT_EQUAL_UINT(0U, bus.transactionCount);
}

void test_partial_raw_write_invalidates_whole_pair_until_full_pair_write() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.writeRegister(cmd::REG_OUTPUT_PORT_0, 0xAAU).ok());
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
  TEST_ASSERT_TRUE((device.shadowValidPairs() & PAIR_OUTPUTS) == 0U);
  const uint32_t before = busTraffic(bus);
  TEST_ASSERT_TRUE(device.writePin(Pin::P00, Level::LOW_LEVEL).is(Err::SHADOW_INVALID));
  assertBusSilentSince(bus, before);

  const uint8_t outputs[2] = {0xAAU, 0x55U};
  TEST_ASSERT_TRUE(device.writeRegisters(cmd::REG_OUTPUT_PORT_0, outputs, 2U).ok());
  TEST_ASSERT_TRUE((device.shadowValidPairs() & PAIR_OUTPUTS) != 0U);
  TEST_ASSERT_TRUE(device.writePin(Pin::P00, Level::HIGH_LEVEL).ok());
  TEST_ASSERT_EQUAL_HEX8(0xAB, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x55, bus.regs[cmd::REG_OUTPUT_PORT_1]);
}

void test_synchronous_input_reads_always_park_pointer_and_return_both_ports() {
  FakeBus bus;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0x12U;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0x34U;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  PortData inputs;
  TEST_ASSERT_TRUE(device.readInputs(inputs).ok());
  TEST_ASSERT_EQUAL_HEX16(0x3412, inputs.combined());
  TEST_ASSERT_EQUAL_UINT(2U, bus.transactionCount);
  assertTransaction(bus, 0U, 'R', cmd::REG_INPUT_PORT_0);
  assertTransaction(bus, 1U, 'W', cmd::ERRATA_SAFE_CMD);
  TEST_ASSERT_EQUAL_HEX8(cmd::ERRATA_SAFE_CMD, bus.pointer);

  clearTransactions(bus);
  uint16_t combined = 0U;
  TEST_ASSERT_TRUE(device.readInputsAndClearInterrupt(combined).ok());
  TEST_ASSERT_EQUAL_HEX16(0x3412, combined);
  TEST_ASSERT_EQUAL_UINT(2U, bus.transactionCount);
}

void test_scalar_observations_clear_pair_validity_until_a_full_pair_read() {
  {
    FakeBus bus;
    PCA9555::PCA9555 device;
    TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
    PortData inputs{};
    TEST_ASSERT_TRUE(device.readInputs(inputs).ok());
    TEST_ASSERT_TRUE(device.lastObservedState().valid(PAIR_INPUTS));

    uint8_t input0 = 0U;
    TEST_ASSERT_TRUE(device.readInput(Port::PORT_0, input0).ok());
    TEST_ASSERT_FALSE(device.lastObservedState().valid(PAIR_INPUTS));

    TEST_ASSERT_TRUE(device.readInputs(inputs).ok());
    TEST_ASSERT_TRUE(device.lastObservedState().valid(PAIR_INPUTS));
  }

  {
    FakeBus bus;
    PCA9555::PCA9555 device;
    bindAndApply(device, bus, image());
    TEST_ASSERT_TRUE(device.lastObservedState().valid(PAIR_OUTPUTS));

    uint8_t output0 = 0U;
    TEST_ASSERT_TRUE(device.readOutput(Port::PORT_0, output0).ok());
    TEST_ASSERT_FALSE(device.lastObservedState().valid(PAIR_OUTPUTS));

    uint8_t pair[2] = {};
    TEST_ASSERT_TRUE(
        device.readRegisters(cmd::REG_OUTPUT_PORT_0, pair, 2U).ok());
    TEST_ASSERT_TRUE(device.lastObservedState().valid(PAIR_OUTPUTS));

    TEST_ASSERT_TRUE(device.readRegister(cmd::REG_OUTPUT_PORT_1, output0).ok());
    TEST_ASSERT_FALSE(device.lastObservedState().valid(PAIR_OUTPUTS));
    TEST_ASSERT_TRUE(
        device.readRegisters(cmd::REG_OUTPUT_PORT_0, pair, 2U).ok());
    TEST_ASSERT_TRUE(device.lastObservedState().valid(PAIR_OUTPUTS));
  }
}

void test_synchronous_input_read_preserves_data_when_park_fails() {
  FakeBus bus;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0xAAU;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0x55U;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  armWriteFault(bus, cmd::ERRATA_SAFE_CMD, TransportCode::NACK_DATA,
                WriteEffect::NOT_ATTEMPTED, 0U, -360);
  PortData inputs{};
  const Status status = device.readInputs(inputs);
  TEST_ASSERT_TRUE(status.is(Err::I2C_NACK_DATA));
  TEST_ASSERT_EQUAL_HEX16(0x55AA, inputs.combined());
  TEST_ASSERT_EQUAL_UINT(2U, bus.transactionCount);
}

void test_register_pair_round_trip_and_odd_start_wrap_match_chip_protocol() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  const uint8_t values[2] = {0x12U, 0x34U};
  TEST_ASSERT_TRUE(device.writeRegisters(cmd::REG_OUTPUT_PORT_1, values, 2U).ok());
  TEST_ASSERT_EQUAL_HEX8(0x34, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.regs[cmd::REG_OUTPUT_PORT_1]);

  // A complete odd-start write establishes the canonical combined shadow as
  // P1:P0 = 0x1234. The following RMW must preserve that byte ordering.
  TEST_ASSERT_TRUE(device.setOutputBits(0x0001U).ok());
  TEST_ASSERT_EQUAL_HEX8(0x35, bus.regs[cmd::REG_OUTPUT_PORT_0]);
  TEST_ASSERT_EQUAL_HEX8(0x12, bus.regs[cmd::REG_OUTPUT_PORT_1]);

  uint8_t readback[2] = {};
  TEST_ASSERT_TRUE(device.readRegisters(cmd::REG_OUTPUT_PORT_1, readback, 2U).ok());
  TEST_ASSERT_EQUAL_HEX8(0x12, readback[0]);
  TEST_ASSERT_EQUAL_HEX8(0x35, readback[1]);
  const ObservedState observed = device.lastObservedState();
  TEST_ASSERT_TRUE(observed.valid(PAIR_OUTPUTS));
  TEST_ASSERT_EQUAL_HEX16(0x1235, observed.registers.outputs);
  TEST_ASSERT_EQUAL_UINT(3U, bus.transactionCount);
}

void test_invalid_preload_pin_is_bus_silent() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  const uint32_t before = busTraffic(bus);
  TEST_ASSERT_TRUE(device.preloadOutput(static_cast<Pin>(16U), Level::LOW_LEVEL)
                       .is(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(device.preloadOutput(static_cast<Pin>(0xFFU), false)
                       .is(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(device.writePin(Pin::P00, static_cast<Level>(2U))
                       .is(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(device.preloadOutput(Pin::P00, static_cast<Level>(0xFFU))
                       .is(Err::INVALID_PARAM));
  assertBusSilentSince(bus, before);
}

void test_read_pin_preserves_output_on_read_failure_but_uses_data_on_park_failure() {
  {
    FakeBus bus;
    PCA9555::PCA9555 device;
    TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
    armReadFault(bus, cmd::REG_INPUT_PORT_0, TransportCode::TIMEOUT, 0U, -380);
    Level level = Level::HIGH_LEVEL;
    TEST_ASSERT_TRUE(device.readPin(Pin::P00, level).is(Err::I2C_TIMEOUT));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Level::HIGH_LEVEL),
                            static_cast<uint8_t>(level));

    armReadFault(bus, cmd::REG_INPUT_PORT_0, TransportCode::BUS_ERROR, 0U, -381);
    bool high = true;
    TEST_ASSERT_TRUE(device.readPin(Pin::P00, high).is(Err::I2C_BUS));
    TEST_ASSERT_TRUE(high);
  }

  {
    FakeBus bus;
    bus.regs[cmd::REG_INPUT_PORT_0] = 0x01U;
    PCA9555::PCA9555 device;
    TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
    armWriteFault(bus, cmd::ERRATA_SAFE_CMD, TransportCode::NACK_DATA,
                  WriteEffect::NOT_ATTEMPTED, 0U, -382);
    Level level = Level::LOW_LEVEL;
    TEST_ASSERT_TRUE(device.readPin(Pin::P00, level).is(Err::I2C_NACK_DATA));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Level::HIGH_LEVEL),
                            static_cast<uint8_t>(level));
  }
}

void test_read_inputs_and_clear_preserves_output_on_read_failure_and_returns_park_data() {
  FakeBus bus;
  bus.regs[cmd::REG_INPUT_PORT_0] = 0x12U;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0x34U;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  uint16_t value = 0U;
  TEST_ASSERT_TRUE(device.readInputsAndClearInterrupt(value).ok());
  TEST_ASSERT_EQUAL_HEX16(0x3412, value);

  armReadFault(bus, cmd::REG_INPUT_PORT_0, TransportCode::TIMEOUT, 0U, -383);
  value = 0xA55AU;
  TEST_ASSERT_TRUE(device.readInputsAndClearInterrupt(value).is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_HEX16(0xA55A, value);

  bus.regs[cmd::REG_INPUT_PORT_0] = 0xABU;
  bus.regs[cmd::REG_INPUT_PORT_1] = 0xCDU;
  armWriteFault(bus, cmd::ERRATA_SAFE_CMD, TransportCode::BUS_ERROR,
                WriteEffect::NOT_ATTEMPTED, 0U, -384);
  value = 0U;
  TEST_ASSERT_TRUE(device.readInputsAndClearInterrupt(value).is(Err::I2C_BUS));
  TEST_ASSERT_EQUAL_HEX16(0xCDAB, value);
}

void test_input_registers_remain_read_only_in_fake_and_public_api() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  const uint32_t before = busTraffic(bus);
  TEST_ASSERT_TRUE(device.writeRegister(cmd::REG_INPUT_PORT_0, 0U)
                       .is(Err::INVALID_PARAM));
  TEST_ASSERT_EQUAL_UINT32(before, busTraffic(bus));
  TEST_ASSERT_EQUAL_HEX8(0xFF, bus.regs[cmd::REG_INPUT_PORT_0]);
}

void test_invalid_register_lengths_ports_and_pins_are_bus_silent() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  const uint32_t before = busTraffic(bus);
  uint8_t value = 0U;
  uint8_t data[3] = {};
  TEST_ASSERT_TRUE(device.readRegister(0x08U, value).is(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(device.readRegisters(cmd::REG_OUTPUT_PORT_0, data, 0U)
                       .is(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(device.readRegisters(cmd::REG_OUTPUT_PORT_0, data, 3U)
                       .is(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(device.writeRegisters(cmd::REG_OUTPUT_PORT_0, data, 3U)
                       .is(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(device.readOutput(static_cast<Port>(2U), value)
                       .is(Err::INVALID_PARAM));
  Level level = Level::LOW_LEVEL;
  TEST_ASSERT_TRUE(device.readPin(static_cast<Pin>(16U), level)
                       .is(Err::INVALID_PARAM));
  assertBusSilentSince(bus, before);
}

void test_transport_short_counts_are_rejected_and_never_fake_success() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  bus.fault = FakeFault{
      true, 'R', cmd::REG_OUTPUT_PORT_0,
      TransportResult{TransportCode::OK, 0, WriteEffect::NOT_APPLICABLE, 1U, 1U},
      0U, 1U};
  PortData outputs{0xAAU, 0xBBU};
  TEST_ASSERT_TRUE(device.readOutputs(outputs).is(Err::I2C_ERROR));
  TEST_ASSERT_EQUAL_HEX8(0xAA, outputs.port0);
  TEST_ASSERT_EQUAL_HEX8(0xBB, outputs.port1);
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);
}

void test_transport_timeout_and_owner_deadline_remain_distinct() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  armReadFault(bus, cmd::REG_OUTPUT_PORT_0, TransportCode::TIMEOUT, 0U, -370);
  PortData outputs;
  TEST_ASSERT_TRUE(device.readOutputs(outputs).is(Err::I2C_TIMEOUT));
  TEST_ASSERT_EQUAL_UINT(1U, bus.transactionCount);

  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.startVerifyImage(81U, image(), 10U, 1U).inProgress());
  uint8_t used = 3U;
  TEST_ASSERT_TRUE(device.pollOperation(81U, 11U, 1U, used).is(Err::TIMEOUT));
  TEST_ASSERT_EQUAL_UINT8(0U, used);
  TEST_ASSERT_EQUAL_UINT(0U, bus.transactionCount);
  (void)takeResult(device, 81U);
}

void test_operation_callback_timeouts_share_remaining_deadline_budget() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  bindAndApply(device, bus, RegisterImage{});
  clearTransactions(bus);
  TEST_ASSERT_TRUE(device.startVerifyImage(811U, RegisterImage{}, 100U, 9U)
                       .inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(811U, 100U, 3U, used).ok());
  TEST_ASSERT_EQUAL_UINT8(3U, used);
  TEST_ASSERT_EQUAL_UINT(3U, bus.transactionCount);
  uint32_t timeoutSum = 0U;
  for (size_t i = 0U; i < bus.transactionCount; ++i) {
    TEST_ASSERT_TRUE(bus.transactions[i].timeoutMs > 0U);
    timeoutSum += bus.transactions[i].timeoutMs;
  }
  TEST_ASSERT_TRUE(timeoutSum <= 9U);
  (void)takeResult(device, 811U);
}

void test_detach_is_passive_repeatable_and_retains_active_cancellation_result() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.startApplyImage(82U, image(), 0U, 100U).inProgress());
  const uint32_t before = busTraffic(bus);
  TEST_ASSERT_TRUE(device.detach().ok());
  assertBusSilentSince(bus, before);
  TEST_ASSERT_FALSE(device.isBound());
  TEST_ASSERT_TRUE(device.detach().ok());
  TEST_ASSERT_TRUE(device.operationResultPending());
  const OperationResult result = takeResult(device, 82U);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationOutcome::CANCELLED),
                          static_cast<uint8_t>(result.outcome));
  TEST_ASSERT_TRUE(device.detach().ok());
  TEST_ASSERT_TRUE(device.end().ok());
  assertBusSilentSince(bus, before);
}

void test_rebind_is_rejected_while_operation_or_result_is_pending() {
  FakeBus first;
  FakeBus second;
  second.address = 0x21U;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(first)).ok());
  TEST_ASSERT_TRUE(device.startApplyImage(83U, image(), 0U, 100U).inProgress());
  TEST_ASSERT_TRUE(device.bind(makeConfig(second)).is(Err::BUSY));
  TEST_ASSERT_TRUE(device.getConfig().i2cUser == &first);
  TEST_ASSERT_TRUE(device.cancelOperation(83U).ok());
  TEST_ASSERT_TRUE(device.bind(makeConfig(second)).is(Err::BUSY));
  (void)takeResult(device, 83U);
  TEST_ASSERT_TRUE(device.bind(makeConfig(second)).ok());
  TEST_ASSERT_TRUE(device.getConfig().i2cUser == &second);
  TEST_ASSERT_EQUAL_UINT32(0U, busTraffic(second));
}

void test_detach_refuses_to_abandon_required_pointer_cleanup() {
  FakeBus bus;
  PCA9555::PCA9555 device;
  TEST_ASSERT_TRUE(device.bind(makeConfig(bus)).ok());
  TEST_ASSERT_TRUE(device.startReadInputs(84U, 0U, 100U).inProgress());
  uint8_t used = 0U;
  TEST_ASSERT_TRUE(device.pollOperation(84U, 0U, 1U, used).inProgress());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OperationPhase::POINTER_PARK),
                          static_cast<uint8_t>(device.operationPhase()));
  const uint32_t before = busTraffic(bus);
  TEST_ASSERT_TRUE(device.detach().is(Err::BUSY));
  TEST_ASSERT_TRUE(device.isBound());
  assertBusSilentSince(bus, before);
  TEST_ASSERT_TRUE(device.cancelOperation(84U).inProgress());
  TEST_ASSERT_TRUE(device.pollOperation(84U, 1U, 1U, used).is(Err::CANCELLED));
  (void)takeResult(device, 84U);
  TEST_ASSERT_TRUE(device.detach().ok());
}

int main() {
  UNITY_BEGIN();

  RUN_TEST(test_status_and_typed_value_helpers);
  RUN_TEST(test_bind_and_begin_are_passive_for_all_valid_addresses);
  RUN_TEST(test_invalid_rebind_preserves_live_binding_without_io);
  RUN_TEST(test_probe_is_explicit_one_transfer_and_health_neutral);
  RUN_TEST(test_por_default_check_is_explicit_and_non_identity);
  RUN_TEST(test_apply_operation_budget_and_exactly_once_result);
  RUN_TEST(test_request_identity_rejects_zero_wrong_and_overlapping_ids_bus_silently);
  RUN_TEST(test_deadline_is_exact_wrap_safe_and_bus_silent);
  RUN_TEST(test_input_cancel_runs_required_pointer_park_before_terminal_result);
  RUN_TEST(test_input_timeout_runs_pointer_park_and_preserves_timeout_cause);
  RUN_TEST(test_cancel_cause_survives_deadline_during_required_pointer_cleanup);
  RUN_TEST(test_input_pointer_park_failure_keeps_valid_input_evidence);
  RUN_TEST(test_active_operation_blocks_synchronous_i2c_and_pointer_park_interleaving);
  RUN_TEST(test_cancel_before_first_transfer_is_immediate_and_bus_silent);
  RUN_TEST(test_cancel_and_explicit_timeout_after_apply_phase_preserve_partial_evidence);
  RUN_TEST(test_apply_reports_failure_at_every_phase_without_hidden_retry);
  RUN_TEST(test_verify_reports_failure_at_every_phase_and_retains_completed_pairs);
  RUN_TEST(test_read_observed_state_failure_returns_only_current_partial_evidence);
  RUN_TEST(test_verify_reports_exact_pair_mismatches_without_changing_expected_state);
  RUN_TEST(test_ambiguous_output_write_is_terminal_and_never_replayed);
  RUN_TEST(test_ambiguous_apply_never_advances_to_unsafe_later_phase);
  RUN_TEST(test_not_attempted_write_failure_is_definite_not_indeterminate);
  RUN_TEST(test_non_ok_transport_cannot_claim_definite_committed_write);
  RUN_TEST(test_matching_verify_reconciles_an_ambiguous_full_commit_without_reapply);
  RUN_TEST(test_mixed_verify_reconciles_matches_and_keeps_mismatch_fenced);
  RUN_TEST(test_invalid_output_shadow_fences_all_output_rmw_paths_without_io);
  RUN_TEST(test_invalid_direction_and_polarity_shadows_fence_relevant_rmw_paths);
  RUN_TEST(test_successful_read_after_por_updates_observed_not_write_shadow);
  RUN_TEST(test_ordinary_pair_reads_fence_only_the_externally_changed_shadow_pair);
  RUN_TEST(test_named_and_raw_reads_fence_the_whole_pair_on_any_observed_mismatch);
  RUN_TEST(test_verify_caller_mismatch_preserves_a_truthful_protocol_shadow);
  RUN_TEST(test_verify_matching_external_image_reconciles_observation_and_shadow);
  RUN_TEST(test_repeated_failures_never_gate_owner_requested_io_or_retry_internally);
  RUN_TEST(test_safe_direction_change_writes_latch_before_direction);
  RUN_TEST(test_typed_port_pin_direction_and_polarity_round_trip);
  RUN_TEST(test_failed_safe_preload_never_advances_direction);
  RUN_TEST(test_failed_direction_after_preload_retains_partial_and_uncertain_evidence);
  RUN_TEST(test_raw_configuration_writes_are_rejected_without_io);
  RUN_TEST(test_partial_raw_write_invalidates_whole_pair_until_full_pair_write);
  RUN_TEST(test_synchronous_input_reads_always_park_pointer_and_return_both_ports);
  RUN_TEST(test_scalar_observations_clear_pair_validity_until_a_full_pair_read);
  RUN_TEST(test_synchronous_input_read_preserves_data_when_park_fails);
  RUN_TEST(test_register_pair_round_trip_and_odd_start_wrap_match_chip_protocol);
  RUN_TEST(test_invalid_preload_pin_is_bus_silent);
  RUN_TEST(test_read_pin_preserves_output_on_read_failure_but_uses_data_on_park_failure);
  RUN_TEST(test_read_inputs_and_clear_preserves_output_on_read_failure_and_returns_park_data);
  RUN_TEST(test_input_registers_remain_read_only_in_fake_and_public_api);
  RUN_TEST(test_invalid_register_lengths_ports_and_pins_are_bus_silent);
  RUN_TEST(test_transport_short_counts_are_rejected_and_never_fake_success);
  RUN_TEST(test_transport_timeout_and_owner_deadline_remain_distinct);
  RUN_TEST(test_operation_callback_timeouts_share_remaining_deadline_budget);
  RUN_TEST(test_detach_is_passive_repeatable_and_retains_active_cancellation_result);
  RUN_TEST(test_rebind_is_rejected_while_operation_or_result_is_pending);
  RUN_TEST(test_detach_refuses_to_abandon_required_pointer_cleanup);

  return UNITY_END();
}

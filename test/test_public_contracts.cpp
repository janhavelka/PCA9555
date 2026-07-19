/// @file test_public_contracts.cpp
/// @brief Compile-time ownership, sizing, and portability contracts.

#include <type_traits>
#include <string_view>

#include "PCA9555/PCA9555.h"

static_assert(!std::is_copy_constructible<::PCA9555::PCA9555>::value,
              "A driver instance has one explicit owner");
static_assert(!std::is_copy_assignable<::PCA9555::PCA9555>::value,
              "A driver instance cannot be copy assigned");
static_assert(!std::is_move_constructible<::PCA9555::PCA9555>::value,
              "A bound driver must remain at a stable address");
static_assert(!std::is_move_assignable<::PCA9555::PCA9555>::value,
              "A bound driver cannot be move assigned");

static_assert(std::is_standard_layout<::PCA9555::Status>::value,
              "Status is a framework-neutral value");
static_assert(std::is_trivially_copyable<::PCA9555::Status>::value,
              "Status owns no memory");
static_assert(std::is_standard_layout<::PCA9555::Config>::value,
              "Config contains only non-owning values");
static_assert(std::is_trivially_copyable<::PCA9555::Config>::value,
              "Config owns no memory");
static_assert(std::is_standard_layout<::PCA9555::TransportResult>::value,
              "TransportResult is a portable adapter contract");
static_assert(std::is_trivially_copyable<::PCA9555::TransportResult>::value,
              "TransportResult owns no memory");
static_assert(std::is_standard_layout<::PCA9555::RegisterImage>::value,
              "RegisterImage is a portable value");
static_assert(std::is_trivially_copyable<::PCA9555::RegisterImage>::value,
              "RegisterImage owns no memory");
static_assert(std::is_standard_layout<::PCA9555::ObservedState>::value,
              "ObservedState is a portable value");
static_assert(std::is_trivially_copyable<::PCA9555::ObservedState>::value,
              "ObservedState owns no memory");
static_assert(std::is_standard_layout<::PCA9555::OperationResult>::value,
              "OperationResult is a portable retained result");
static_assert(std::is_trivially_copyable<::PCA9555::OperationResult>::value,
              "OperationResult retains no caller-owned pointers");

static_assert(sizeof(::PCA9555::Err) == sizeof(uint8_t),
              "Error values have fixed width");
static_assert(sizeof(::PCA9555::TransportCode) == sizeof(uint8_t),
              "Transport codes have fixed width");
static_assert(sizeof(::PCA9555::WriteEffect) == sizeof(uint8_t),
              "Write effects have fixed width");
static_assert(sizeof(::PCA9555::Pin) == sizeof(uint8_t),
              "Pin values have fixed width");
static_assert(sizeof(::PCA9555::OperationKind) == sizeof(uint8_t),
              "Operation kinds have fixed width");
static_assert(sizeof(::PCA9555::OperationPhase) == sizeof(uint8_t),
              "Operation phases have fixed width");
static_assert(sizeof(::PCA9555::OperationOutcome) == sizeof(uint8_t),
              "Operation outcomes have fixed width");
static_assert(sizeof(::PCA9555::PinMask) == sizeof(uint16_t),
              "Pin masks cover exactly sixteen pins");
static_assert(sizeof(::PCA9555::RegisterImage) == 3U * sizeof(uint16_t),
              "Register images contain only the three writable pairs");
static_assert(std::is_same<decltype(::PCA9555::Config{}.i2cWrite),
                           ::PCA9555::I2cWriteFn>::value,
              "Write callbacks return one terminal typed result");
static_assert(std::is_same<decltype(::PCA9555::Config{}.i2cWriteRead),
                           ::PCA9555::I2cWriteReadFn>::value,
              "Read callbacks return one terminal typed result");
static_assert(::PCA9555::MAX_APPLY_IMAGE_TRANSACTIONS == 8U,
              "Apply-image work stays explicitly bounded");
static_assert(::PCA9555::MAX_VERIFY_IMAGE_TRANSACTIONS == 3U,
              "Verify-image work stays explicitly bounded");
static_assert(::PCA9555::MAX_READ_INPUTS_TRANSACTIONS == 2U,
              "Input-read cleanup stays explicitly bounded");
static_assert(std::string_view(::PCA9555::errorName(::PCA9555::Err::OK)) == "OK",
              "the first public error code has a stable name");
static_assert(std::string_view(
                  ::PCA9555::errorName(::PCA9555::Err::I2C_NACK_ADDR)) ==
                  "I2C_NACK_ADDR",
              "representative transport error names are stable");
static_assert(std::string_view(
                  ::PCA9555::errorName(::PCA9555::Err::OFFLINE)) ==
                  "OFFLINE_RESERVED",
              "the reserved compatibility code remains explicit");
static_assert(std::string_view(
                  ::PCA9555::errorName(::PCA9555::Err::UNSUPPORTED)) ==
                  "UNSUPPORTED",
              "the last public error code has a stable name");
static_assert(std::string_view(
                  ::PCA9555::errorName(static_cast<::PCA9555::Err>(0xFFU))) ==
                  "UNKNOWN",
              "out-of-range error values have a bounded diagnostic name");

constexpr ::PCA9555::RegisterImage CONTRACT_IMAGE{0x8001U, 0x0000U, 0xFFFEU};
constexpr ::PCA9555::ObservedState observedWith(uint8_t validPairs) {
  ::PCA9555::ObservedState observed{};
  observed.validPairs = validPairs;
  return observed;
}
constexpr ::PCA9555::ObservedState PARTIAL_OBSERVED =
    observedWith(::PCA9555::PAIR_OUTPUTS);
constexpr ::PCA9555::ObservedState WRITABLE_OBSERVED =
    observedWith(::PCA9555::PAIR_ALL_WRITABLE);
constexpr ::PCA9555::ObservedState FULL_OBSERVED =
    observedWith(::PCA9555::PAIR_ALL);
static_assert(::PCA9555::pinIndex(::PCA9555::Pin::P00) == 0U, "P00 index");
static_assert(::PCA9555::pinIndex(::PCA9555::Pin::P17) == 15U, "P17 index");
static_assert(::PCA9555::pinMask(::PCA9555::Pin::P00) == 0x0001U, "P00 mask");
static_assert(::PCA9555::pinMask(::PCA9555::Pin::P17) == 0x8000U, "P17 mask");
static_assert(::PCA9555::portOf(::PCA9555::Pin::P10) == ::PCA9555::Port::PORT_1,
              "P10 port");
static_assert(::PCA9555::bitOf(::PCA9555::Pin::P17) == 7U, "P17 bit");
static_assert(::PCA9555::isOutput(CONTRACT_IMAGE, ::PCA9555::Pin::P00),
              "typed direction helper");
static_assert(::PCA9555::levelFor(CONTRACT_IMAGE, ::PCA9555::Pin::P17) ==
                  ::PCA9555::Level::HIGH_LEVEL,
              "typed level helper");
static_assert(::PCA9555::PortData::fromCombined(0xA55AU).combined() == 0xA55AU,
              "PortData conversion is constexpr and byte-stable");
static_assert(PARTIAL_OBSERVED.valid(::PCA9555::PAIR_OUTPUTS),
              "a complete single pair is valid");
static_assert(!PARTIAL_OBSERVED.valid(::PCA9555::PAIR_ALL_WRITABLE),
              "partial writable evidence is not a complete writable image");
static_assert(!PARTIAL_OBSERVED.valid(::PCA9555::PAIR_ALL),
              "partial evidence is not a complete device observation");
static_assert(!PARTIAL_OBSERVED.valid(::PCA9555::PAIR_NONE),
              "an empty validity request is never evidence");
static_assert(WRITABLE_OBSERVED.valid(::PCA9555::PAIR_ALL_WRITABLE),
              "all writable pairs satisfy the writable composite");
static_assert(!WRITABLE_OBSERVED.valid(::PCA9555::PAIR_ALL),
              "writable pairs alone do not include inputs");
static_assert(!WRITABLE_OBSERVED.valid(::PCA9555::PAIR_NONE),
              "PAIR_NONE remains false for writable evidence");
static_assert(FULL_OBSERVED.valid(::PCA9555::PAIR_ALL_WRITABLE),
              "full evidence includes every writable pair");
static_assert(FULL_OBSERVED.valid(::PCA9555::PAIR_ALL),
              "full evidence satisfies the full composite");
static_assert(!FULL_OBSERVED.valid(::PCA9555::PAIR_NONE),
              "PAIR_NONE remains false for full evidence");

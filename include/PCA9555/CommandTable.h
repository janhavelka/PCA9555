/// @file CommandTable.h
/// @brief Register addresses and bit definitions for PCA9555
#pragma once

#include <cstdint>

namespace PCA9555 {
namespace cmd {

// ============================================================================
// Register Addresses (Command Bytes)
// ============================================================================
// Only bits [2:0] of the command byte are used. Upper bits must be 0.
// Registers are organized as 4 pairs (Port 0 / Port 1).
// Auto-increment alternates within a pair only, not across pairs.
// ============================================================================

/// Input Port 0 — reflects logic level of P00–P07 (read only)
static constexpr uint8_t REG_INPUT_PORT_0 = 0x00;

/// Input Port 1 — reflects logic level of P10–P17 (read only)
static constexpr uint8_t REG_INPUT_PORT_1 = 0x01;

/// Output Port 0 — latched output value for P00–P07 (R/W, default 0xFF)
static constexpr uint8_t REG_OUTPUT_PORT_0 = 0x02;

/// Output Port 1 — latched output value for P10–P17 (R/W, default 0xFF)
static constexpr uint8_t REG_OUTPUT_PORT_1 = 0x03;

/// Polarity Inversion Port 0 — invert input sense for P00–P07 (R/W, default 0x00)
static constexpr uint8_t REG_POLARITY_INV_0 = 0x04;

/// Polarity Inversion Port 1 — invert input sense for P10–P17 (R/W, default 0x00)
static constexpr uint8_t REG_POLARITY_INV_1 = 0x05;

/// Configuration Port 0 — direction for P00–P07 (R/W, default 0xFF)
/// Bit = 1: input (high-Z, internal pullup active)
/// Bit = 0: output (push-pull, driven by Output Port register)
static constexpr uint8_t REG_CONFIG_PORT_0 = 0x06;

/// Configuration Port 1 — direction for P10–P17 (R/W, default 0xFF)
/// Bit = 1: input (high-Z, internal pullup active)
/// Bit = 0: output (push-pull, driven by Output Port register)
static constexpr uint8_t REG_CONFIG_PORT_1 = 0x07;

// ============================================================================
// Register Defaults (after Power-On Reset)
// ============================================================================

/// Default output port value (all high)
static constexpr uint8_t DEFAULT_OUTPUT = 0xFF;

/// Default polarity inversion (no inversion)
static constexpr uint8_t DEFAULT_POLARITY = 0x00;

/// Default configuration (all inputs)
static constexpr uint8_t DEFAULT_CONFIG = 0xFF;

// ============================================================================
// I2C Address
// ============================================================================
// 7-bit address format: 0 1 0 0 A2 A1 A0
// Range: 0x20 (A2=A1=A0=L) to 0x27 (A2=A1=A0=H)
// ============================================================================

/// Base I2C address (all address pins low)
static constexpr uint8_t BASE_ADDRESS = 0x20;

/// Maximum I2C address (all address pins high)
static constexpr uint8_t MAX_ADDRESS = 0x27;

/// Number of register pairs
static constexpr uint8_t NUM_REGISTER_PAIRS = 4;

/// Total number of registers
static constexpr uint8_t NUM_REGISTERS = 8;

/// Number of I/O pins per port
static constexpr uint8_t PINS_PER_PORT = 8;

/// Total number of I/O pins
static constexpr uint8_t TOTAL_PINS = 16;

// ============================================================================
// Interrupt Errata
// ============================================================================
// After reading Input Ports, the register pointer is left at 0x00/0x01.
// If another slave on the bus acknowledges a read, INT may falsely de-assert.
// Workaround: write a command byte != 0x00 after reading input ports.
// We use the Output Port 0 register address as a safe parking address.
// ============================================================================

/// Safe command byte to write after reading input ports (interrupt errata workaround)
static constexpr uint8_t ERRATA_SAFE_CMD = REG_OUTPUT_PORT_0;

} // namespace cmd
} // namespace PCA9555

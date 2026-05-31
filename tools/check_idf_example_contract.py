#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import runpy
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

FORBIDDEN_TOKENS = [
    "ArduinoCompat",
    "IdfArduinoCompat",
    "Arduino.h",
    "Wire.h",
    "String",
    "Serial",
    "TwoWire",
    "examples/01_basic_bringup_cli/main.cpp",
]

REQUIRED_NATIVE_TOKENS = [
    'extern "C" void app_main(void)',
    "driver/i2c_master.h",
    "esp_timer_get_time",
    "vTaskDelay",
    "fgets",
    "i2c_new_master_bus",
]

FORBIDDEN_PLACEHOLDER_TEXT = [
    "Command is present in the native IDF contract; use help for arguments.",
    "Command is intentionally blocked in this native IDF example",
    "Use hardware-specific test firmware for destructive/output-driving workflows.",
]

REQUIRED_CONFIRMATION_TOKENS = [
    "Confirmation required.",
    "Would change:",
    "Why confirmation is required:",
    "Confirmed command:",
    "parseConfirmSuffix",
    "requireConfirmation",
]

REQUIRED_IDF_SURFACE_TOKENS = [
    "i2c_master_dev_handle_t device",
    "ensureDevice",
    "cmdWritePin",
    "gDev.writePin",
    "cmdTogglePin",
    "gDev.togglePin",
    "cmdSetDirection",
    "gDev.setPinDirection",
    "cmdWritePort",
    "gDev.writeOutput",
    "cmdSetPortDirection",
    "gDev.setPortConfiguration",
    "cmdSetPinPolarity",
    "gDev.setPinPolarity",
    "cmdSetPortPolarity",
    "gDev.setPortPolarity",
    "MaskCommand::SET_OUTPUT",
    "gDev.setOutputBits",
    "gDev.clearOutputBits",
    "gDev.toggleOutputBits",
    "gDev.configureInputBits",
    "gDev.configureOutputBits",
    "gDev.setInvertBits",
    "gDev.clearInvertBits",
    "cmdWriteReg",
    "gDev.writeRegister",
    "cmdWriteRegs",
    "gDev.writeRegisters",
    "cmdPattern",
    "cmdSweep",
    "cmdWalk",
    "cmdSelfTest",
    "cmdStress",
    "cmdStressMix",
    "cmdRecover",
]

REQUIRED_CONFIRMED_HELP_TEXT = [
    "write pin <N> <0|1> / wpin <N> <0|1> [confirm]",
    "dir pin <N> <in|out> / dir <N> <in|out> [confirm]",
    "write port <P> <V> / wport <P> <V> [confirm]",
    "dir port <P> <V> / dport <P> <V> [confirm]",
    "polarity pin <N> <0|1> / pol <N> <0|1> [confirm]",
    "polarity port <P> <V> / wpol <P> <V> [confirm]",
    "write reg <R> <V> / wreg <R> <V> [confirm]",
    "pattern <VALUE> / pat <VALUE> [confirm]",
    "recover [confirm]",
    "selftest [confirm] | stress [N] [confirm] | stress_mix [N] [confirm]",
]


def fail(msg: str) -> None:
    print(f"IDF example contract FAILED: {msg}")
    raise SystemExit(1)


def main() -> int:
    ns = runpy.run_path(str(ROOT / "tools" / "check_cli_contract.py"))
    commands = ns.get("MANDATORY_COMMANDS", [])
    components = ns.get("IDF_REQUIRED_COMPONENTS", [])
    main_path = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"
    cmake_path = ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt"
    text = main_path.read_text(encoding="utf-8", errors="replace")
    cmake = cmake_path.read_text(encoding="utf-8", errors="replace")

    for token in FORBIDDEN_TOKENS:
        if token in text:
            fail(f"forbidden Arduino compatibility token in IDF example: {token}")
    for token in REQUIRED_NATIVE_TOKENS:
        if token not in text:
            fail(f"native ESP-IDF token missing: {token}")
    for token in FORBIDDEN_PLACEHOLDER_TEXT:
        if token in text:
            fail(f"old placeholder command text still present: {token}")
    for token in REQUIRED_CONFIRMATION_TOKENS:
        if token not in text:
            fail(f"confirmation guard token missing: {token}")
    for token in REQUIRED_IDF_SURFACE_TOKENS:
        if token not in text:
            fail(f"native IDF command surface token missing: {token}")
    for token in REQUIRED_CONFIRMED_HELP_TEXT:
        if token not in text:
            fail(f"confirmed command help text missing: {token}")
    for cmd in commands:
        if cmd == "?":
            if '"?"' not in text and " / ?" not in text and " | ?" not in text:
                fail("mandatory command '?' missing from IDF example")
        elif re.search(rf"\b{re.escape(cmd)}\b", text) is None:
            fail(f"mandatory command '{cmd}' missing from IDF example")
    for component in components:
        if re.search(rf"\b{re.escape(component)}\b", cmake) is None:
            fail(f"ESP-IDF CMake file missing component '{component}'")

    print("IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

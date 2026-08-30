#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import re
import runpy
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "examples" / "espidf_basic"
MAIN = EXAMPLE / "main" / "main.cpp"
README = EXAMPLE / "README.md"
LEGACY_EXAMPLE = ROOT / "examples" / "esp_idf" / "basic"

REQUIRED_FILES = [
    ROOT / "CMakeLists.txt",
    ROOT / "idf_component.yml",
    EXAMPLE / "CMakeLists.txt",
    EXAMPLE / "main" / "CMakeLists.txt",
    MAIN,
    README,
]

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
    "driver/uart_vfs.h",
    "esp_timer_get_time",
    "vTaskDelay",
    "fgets",
    "i2c_new_master_bus",
    "initConsole",
    "uart_vfs_dev_use_driver",
    "usb_serial_jtag_vfs_use_driver",
    "CONFIG_ESP_CONSOLE_USB_CDC",
]

REQUIRED_COMPONENTS = [
    "PCA9555",
    "esp_driver_i2c",
    "esp_driver_gpio",
    "esp_driver_uart",
    "esp_driver_usb_serial_jtag",
    "esp_timer",
    "esp_vfs_console",
    "freertos",
    "vfs",
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
    "write reg <2-5> <V> / wreg <2-5> <V> [confirm]",
    "write regs <2-5> <V0> [V1] / wregs <2-5> <V0> [V1] [confirm]",
    "pattern <VALUE> / pat <VALUE> [confirm]",
    "recover [confirm]",
    "selftest [confirm] | stress [N] [confirm] | stress_mix [N] [confirm]",
]


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def fail(violations: list[str], message: str) -> None:
    violations.append(message)


def contains(pattern: str, text: str) -> bool:
    return re.search(pattern, text, re.MULTILINE) is not None


def main() -> int:
    violations: list[str] = []

    if LEGACY_EXAMPLE.exists():
        fail(violations, "stale duplicate ESP-IDF example path exists: examples/esp_idf/basic")
    for path in REQUIRED_FILES:
        if not path.exists():
            fail(violations, f"missing required file: {path.relative_to(ROOT).as_posix()}")

    if violations:
        print("ESP-IDF example contract FAILED:")
        for item in violations:
            print(f"- {item}")
        return 1

    root_cmake = read(ROOT / "CMakeLists.txt")
    component_yml = read(ROOT / "idf_component.yml")
    library_json = json.loads(read(ROOT / "library.json"))
    example_cmake = read(EXAMPLE / "CMakeLists.txt")
    main_cmake = read(EXAMPLE / "main" / "CMakeLists.txt")
    main_cpp = read(MAIN)
    readme = read(README)

    if "idf_component_register" not in root_cmake:
        fail(violations, "root CMakeLists.txt must register an ESP-IDF component")
    if "src/PCA9555.cpp" not in root_cmake or "include" not in root_cmake:
        fail(violations, "root component must compile src/PCA9555.cpp and expose include/")
    if re.search(r"\b(Arduino|Wire)\b", root_cmake + component_yml):
        fail(violations, "IDF component metadata must not depend on Arduino or Wire")
    manifest_version = re.search(r'^version:\s*"([^"]+)"\s*$', component_yml, re.MULTILINE)
    if not manifest_version:
        fail(violations, "idf_component.yml must declare a quoted version")
    elif manifest_version.group(1) != library_json.get("version"):
        fail(violations, "idf_component.yml version must match library.json version")

    if "EXTRA_COMPONENT_DIRS" not in example_cmake:
        fail(violations, "IDF example must consume the repository as an external component")
    for component in REQUIRED_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", main_cmake) is None:
            fail(violations, f"ESP-IDF CMake file missing component '{component}'")

    for token in FORBIDDEN_TOKENS:
        if token in main_cpp:
            fail(violations, f"forbidden Arduino compatibility token in IDF example: {token}")
    for token in REQUIRED_NATIVE_TOKENS:
        if token not in main_cpp:
            fail(violations, f"native ESP-IDF token missing: {token}")
    for token in FORBIDDEN_PLACEHOLDER_TEXT:
        if token in main_cpp:
            fail(violations, f"old placeholder command text still present: {token}")
    for token in REQUIRED_CONFIRMATION_TOKENS:
        if token not in main_cpp:
            fail(violations, f"confirmation guard token missing: {token}")
    for token in REQUIRED_IDF_SURFACE_TOKENS:
        if token not in main_cpp:
            fail(violations, f"native IDF command surface token missing: {token}")
    for token in REQUIRED_CONFIRMED_HELP_TEXT:
        if token not in main_cpp:
            fail(violations, f"confirmed command help text missing: {token}")
    for token in (
        "write reg <0x02..0x05> <0x00..0xFF> [confirm]",
        "write regs <0x02..0x05> <0x00..0xFF> [0x00..0xFF] [confirm]",
    ):
        if token not in main_cpp:
            fail(violations, f"raw write range token missing: {token}")
    for stale in ("0x02..0x07", "<2-7>"):
        if stale in main_cpp:
            fail(violations, f"native IDF CLI advertises unsupported raw Configuration writes: {stale}")

    ns = runpy.run_path(str(ROOT / "tools" / "check_cli_contract.py"))
    for cmd in ns.get("MANDATORY_COMMANDS", []):
        if cmd == "?":
            if '"?"' not in main_cpp and " / ?" not in main_cpp and " | ?" not in main_cpp:
                fail(violations, "mandatory command '?' missing from IDF example")
        elif re.search(rf"\b{re.escape(cmd)}\b", main_cpp) is None:
            fail(violations, f"mandatory command '{cmd}' missing from IDF example")
    lower_readme = readme.lower()
    for required in ("confirm", "not hardware validation", "arduino", "wire", "idf.py"):
        if required not in lower_readme:
            fail(violations, f"IDF example README must document: {required}")

    if violations:
        print("ESP-IDF example contract FAILED:")
        for item in violations:
            print(f"- {item}")
        return 1

    print("ESP-IDF example contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

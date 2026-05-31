#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "examples" / "esp_idf" / "basic"
MAIN = EXAMPLE / "main" / "main.cpp"
README = EXAMPLE / "README.md"

REQUIRED_FILES = [
    ROOT / "CMakeLists.txt",
    ROOT / "idf_component.yml",
    EXAMPLE / "CMakeLists.txt",
    EXAMPLE / "main" / "CMakeLists.txt",
    MAIN,
    README,
    EXAMPLE / "sdkconfig.defaults",
]


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def fail(violations: list[str], message: str) -> None:
    violations.append(message)


def contains(pattern: str, text: str) -> bool:
    return re.search(pattern, text, re.MULTILINE) is not None


def main() -> int:
    violations: list[str] = []

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
    if 'idf: ">=5.4,<6.0"' not in component_yml:
        fail(violations, "idf_component.yml must constrain ESP-IDF support to the validated v5.4 line")

    if "EXTRA_COMPONENT_DIRS" not in example_cmake:
        fail(violations, "IDF example must consume the repository as an external component")
    if "PCA9555" not in main_cmake:
        fail(violations, "IDF example main component must require the PCA9555 component")
    if "esp_driver_i2c" not in main_cmake:
        fail(violations, "IDF example must require the native ESP-IDF I2C driver component")

    forbidden_includes = [
        r'^\s*#\s*include\s*[<"]Arduino\.h[>"]',
        r'^\s*#\s*include\s*[<"]Wire\.h[>"]',
    ]
    for pattern in forbidden_includes:
        if contains(pattern, main_cpp):
            fail(violations, "IDF example must not include Arduino.h or Wire.h")

    required_main_patterns = {
        "app_main": r'extern\s+"C"\s+void\s+app_main\s*\(',
        "native IDF I2C header": r'driver/i2c_master\.h',
        "native IDF I2C bus init": r"i2c_new_master_bus",
        "native IDF I2C transactions": r"i2c_master_transmit",
        "external bus context": r"struct\s+IdfI2cContext",
        "i2c user context": r"cfg\.i2cUser\s*=",
        "write callback": r"cfg\.i2cWrite\s*=",
        "write-read callback": r"cfg\.i2cWriteRead\s*=",
        "timeout conversion": r"timeoutToMs",
        "IDF timeout mapping": r"ESP_ERR_TIMEOUT[\s\S]*I2C_TIMEOUT",
        "IDF config mapping": r"ESP_ERR_INVALID_(ARG|STATE)[\s\S]*INVALID_CONFIG",
        "NACK/unknown-phase mapping": r"(ESP_ERR_INVALID_RESPONSE|ESP_FAIL)[\s\S]*I2C_ERROR",
        "address NACK mapping": r"I2C_NACK_ADDR",
        "safe output API": r"configureOutputs",
        "lock hook": r"cfg\.i2cLock\s*=",
        "unlock hook": r"cfg\.i2cUnlock\s*=",
    }
    for name, pattern in required_main_patterns.items():
        if not contains(pattern, main_cpp):
            fail(violations, f"IDF example missing {name}")

    lower_readme = readme.lower()
    for required in ("timeout", "not a production", "not hardware validation", "arduino", "wire"):
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

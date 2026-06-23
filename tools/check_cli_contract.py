#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import runpy
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_COMMON = [
    "BoardConfig.h",
    "BuildConfig.h",
    "Log.h",
    "I2cTransport.h",
    "I2cScanner.h",
    "CommandHandler.h",
    "TransportAdapter.h",
    "BusDiag.h",
    "CliShell.h",
    "CliStyle.h",
    "HealthView.h",
    "HealthDiag.h",
]

MANDATORY_COMMANDS = [
    "help",
    "?",
    "version",
    "ver",
    "scan",
    "read",
    "inputs",
    "rin",
    "outputs",
    "config",
    "polarity",
    "cfg",
    "settings",
    "rpin",
    "rout",
    "rdir",
    "rpol",
    "pininfo",
    "pins",
    "rreg",
    "rregs",
    "dump",
    "wpin",
    "toggle",
    "dir",
    "wport",
    "dport",
    "pol",
    "wpol",
    "setbits",
    "sb",
    "clearbits",
    "cb",
    "togglebits",
    "tb",
    "dirin",
    "dirout",
    "invertset",
    "invertclr",
    "wreg",
    "wregs",
    "drv",
    "health",
    "probe",
    "recover",
    "verbose",
    "selftest",
    "sweep",
    "walk",
    "allhigh",
    "alllow",
    "pattern",
    "pat",
    "stress_mix",
    "stress",
]

IDF_REQUIRED_COMPONENTS = [
    "PCA9555",
    "esp_driver_i2c",
    "esp_driver_gpio",
    "esp_timer",
    "freertos",
    "vfs",
]

CONFIRM_HELP_SNIPPETS = [
    "wpin <N> <0|1> [confirm]",
    "toggle <N> [confirm]",
    "dir <N> <in|out> [confirm]",
    "wport <P> <V> [confirm]",
    "dport <P> <V> [confirm]",
    "pol <N> <0|1> [confirm]",
    "wpol <P> <V> [confirm]",
    "setbits <M> / sb <M> [confirm]",
    "clearbits <M> / cb <M> [confirm]",
    "togglebits <M> / tb <M> [confirm]",
    "dirin <M> [confirm]",
    "dirout <M> [confirm]",
    "invertset <M> [confirm]",
    "invertclr <M> [confirm]",
    "wreg <R> <V> [confirm]",
    "wregs <R> <V0> [V1] [confirm]",
    "pattern <VALUE> / pat <VALUE> [confirm]",
    "sweep [delay_ms] [confirm]",
    "walk [delay_ms] [confirm]",
    "allhigh [confirm]",
    "alllow [confirm]",
    "recover [confirm]",
    "selftest [confirm]",
    "stress_mix [N] [confirm]",
]


def fail(msg: str) -> None:
    print(f"CLI contract FAILED: {msg}")
    raise SystemExit(1)


def ensure_exists(path: pathlib.Path, label: str) -> None:
    if not path.exists():
        fail(f"missing {label}: {path.as_posix()}")


def ensure_missing(path: pathlib.Path, label: str) -> None:
    if path.exists():
        fail(f"forbidden {label} still present: {path.as_posix()}")


def require_token(text: str, token: str, label: str) -> None:
    if token == "?":
        if '"?"' not in text:
            fail(f"{label} '{token}' missing")
        return
    if re.search(rf"\b{re.escape(token)}\b", text) is None:
        fail(f"{label} '{token}' missing")


def require_dispatch(text: str, token: str) -> None:
    quoted = re.escape(f'"{token}"')
    starts_with_arg = rf'"{re.escape(token)}(?:\s|")'
    patterns = [
        rf"cmd\s*==\s*{quoted}",
        rf"cmd\.startsWith\(\s*{starts_with_arg}",
    ]
    if not any(re.search(pattern, text) for pattern in patterns):
        fail(f"mandatory command '{token}' missing from processCommand() dispatch")


def require_help(text: str, token: str) -> None:
    if token == "?":
        return
    pattern = rf"printHelpItem\s*\(\s*\"[^\"]*\b{re.escape(token)}\b"
    if re.search(pattern, text) is None:
        fail(f"mandatory command '{token}' missing from help text")


def require_confirmation_contract(text: str) -> None:
    for token in (
        "Confirmation required.",
        "Confirmed command:",
        "stripConfirmSuffix",
        "requireConfirmation",
        "requireExactConfirmation",
    ):
        if token not in text:
            fail(f"Arduino CLI confirmation contract token missing: {token}")
    for snippet in CONFIRM_HELP_SNIPPETS:
        if snippet not in text:
            fail(f"Arduino CLI mutating help is missing confirm suffix: {snippet}")
    if text.count("requireConfirmation(") < 20:
        fail("Arduino CLI mutating dispatch must route through requireConfirmation()")


def main() -> int:
    common_dir = ROOT / "examples" / "common"
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"
    idf_main = ROOT / "examples" / "espidf_basic" / "main" / "main.cpp"
    idf_cmake = ROOT / "examples" / "espidf_basic" / "main" / "CMakeLists.txt"

    ensure_exists(common_dir, "common example directory")
    ensure_exists(bringup_main, "bringup CLI example")
    ensure_exists(idf_main, "ESP-IDF bringup entry point")
    ensure_exists(idf_cmake, "ESP-IDF bringup CMake file")

    ensure_missing(ROOT / "examples" / "00_smoke_boot", "deprecated example 00_smoke_boot")
    ensure_missing(
        ROOT / "examples" / "03_feature_walkthrough",
        "deprecated example 03_feature_walkthrough",
    )

    for name in REQUIRED_COMMON:
        ensure_exists(common_dir / name, f"common helper {name}")

    text = bringup_main.read_text(encoding="utf-8", errors="replace")

    for cmd in MANDATORY_COMMANDS:
        require_token(text, cmd, "mandatory command")
        require_dispatch(text, cmd)
        require_help(text, cmd)
    require_confirmation_contract(text)

    idf_text = idf_main.read_text(encoding="utf-8", errors="replace")
    if 'extern "C" void app_main(void)' not in idf_text:
        fail("ESP-IDF entry point must define app_main()")

    cmake_text = idf_cmake.read_text(encoding="utf-8", errors="replace")
    for component in IDF_REQUIRED_COMPONENTS:
        if re.search(rf"\b{re.escape(component)}\b", cmake_text) is None:
            fail(f"ESP-IDF CMake file missing required component '{component}'")

    idf_contract = runpy.run_path(str(ROOT / "tools" / "check_idf_example_contract.py"))
    idf_contract["main"]()

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

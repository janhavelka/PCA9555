#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

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
    "wreg <2-5> <V> [confirm]",
    "wregs <2-5> <V0> [V1] [confirm]",
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
    for stale in ("wreg <2-7>", "wregs <2-7>", "Write register R (2-7)"):
        if stale in text:
            fail(f"Arduino CLI advertises unsupported raw Configuration writes: {stale}")
    if text.count("requireConfirmation(") < 20:
        fail("Arduino CLI mutating dispatch must route through requireConfirmation()")


def main() -> int:
    bringup_main = ROOT / "examples" / "01_basic_bringup_cli" / "main.cpp"

    ensure_exists(bringup_main, "bringup CLI example")

    ensure_missing(ROOT / "examples" / "00_smoke_boot", "deprecated example 00_smoke_boot")
    ensure_missing(
        ROOT / "examples" / "03_feature_walkthrough",
        "deprecated example 03_feature_walkthrough",
    )

    text = bringup_main.read_text(encoding="utf-8", errors="replace")

    for cmd in MANDATORY_COMMANDS:
        require_token(text, cmd, "mandatory command")
        require_dispatch(text, cmd)
        require_help(text, cmd)
    require_confirmation_contract(text)

    print("CLI contract PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

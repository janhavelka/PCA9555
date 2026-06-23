#!/usr/bin/env python3
"""Static contract guard for PCA9555 HIL runner/docs."""

from __future__ import annotations

import importlib.util
import pathlib
import py_compile
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "run_i2c_hil.py"
HARDWARE_DOC = ROOT / "docs" / "hardware_validation.md"
README = ROOT / "README.md"
GITIGNORE = ROOT / ".gitignore"


def fail(message: str) -> None:
    print(f"check_hil_contract: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(path: pathlib.Path) -> str:
    if not path.exists():
        fail(f"missing required file: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8", errors="replace")


def load_runner_module():
    py_compile.compile(str(RUNNER), doraise=True)
    spec = importlib.util.spec_from_file_location("run_i2c_hil_contract", RUNNER)
    if spec is None or spec.loader is None:
        fail("could not import run_i2c_hil.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def main() -> int:
    runner_text = read(RUNNER)
    hardware_text = read(HARDWARE_DOC)
    readme_text = read(README)
    gitignore_text = read(GITIGNORE)

    if "hil_logs/" not in gitignore_text.splitlines():
        fail(".gitignore must contain hil_logs/")
    for required in (
        "--dry-run",
        "--parser-self-test",
        "--port",
        "--baud",
        "--out",
        "--timeout",
        "--timeout-s",
        "--idle-timeout-s",
        "--allow-idle-completion",
        "--boot-settle-s",
        "--reconnect-attempts",
        "--benchmark-command",
        "--soak-duration-s",
        "--report",
    ):
        if required not in runner_text:
            fail(f"runner is missing required CLI option {required}")
    if "python -m pip install pyserial" not in runner_text:
        fail("runner is missing pyserial install guidance")

    module = load_runner_module()
    default_commands = [spec.command for spec in module.DEFAULT_SAFE_COMMANDS]
    unsafe_default = [
        spec.command
        for spec in module.DEFAULT_SAFE_COMMANDS
        if spec.destructive or spec.requires_opt_in or spec.operator_check
    ]
    if unsafe_default:
        fail(f"unsafe commands in DEFAULT_SAFE_COMMANDS: {unsafe_default}")

    documented = []
    in_block = False
    for line in hardware_text.splitlines():
        if line.strip() == "<!-- HIL_COMMAND_SEQUENCE_START -->":
            in_block = True
            continue
        if line.strip() == "<!-- HIL_COMMAND_SEQUENCE_END -->":
            in_block = False
            continue
        if in_block and line.startswith("- `"):
            documented.append(line.split("`", 2)[1])
    if documented != default_commands:
        fail(f"documented command sequence differs from runner: {documented} != {default_commands}")

    unsafe_words = ("pattern ", "allhigh", "alllow", "walk ", "sweep ", "stress_mix", "wpin", "wport")
    for word in unsafe_words:
        if word.strip() in default_commands:
            fail(f"unsafe command {word!r} appears in default sequence")
    for command in ("allhigh", "pattern 0xAAAA", "wport 0 0x00", "stress_mix 1"):
        spec = module.custom_command_spec(command, 5.0)
        if not spec.destructive or spec.requires_opt_in != "--include-output-tests":
            fail(f"custom unsafe command is not gated: {command}")
    if module.final_verdict(
        [
            module.CommandResult(
                command="probe",
                purpose="test",
                classifier="probe",
                serial_result="PASS",
                operator_result="N/A",
                completion_reason="prompt",
                elapsed_s=0.0,
                notes="",
                evidence=[],
            )
        ],
        False,
    ) != "OPERATOR_REVIEW_REQUIRED":
        fail("final verdict must not be PASS while manual HIL checks remain open")

    for docs_name, docs_text in {
        "hardware validation": hardware_text,
    }.items():
        for phrase in (
            "OPERATOR_CHECK_REQUIRED",
            "No physical HIL validation was performed",
            "tools/run_i2c_hil.py",
            "hil_logs/",
        ):
            if phrase not in docs_text:
                fail(f"{docs_name} missing required phrase: {phrase}")
        if not re.search(r"ACK.*only|only.*ACK", docs_text, flags=re.IGNORECASE | re.DOTALL):
            fail(f"{docs_name} must state that ACK is not chip identity")

    forbidden_claims = (
        "hardware validation passed",
        "field-grade validation passed",
        "industry-grade validation passed",
        "production validation passed",
        "hardware tests passed",
    )
    combined_docs = "\n".join([hardware_text, readme_text])
    for claim in forbidden_claims:
        if claim in combined_docs.lower():
            fail(f"unsupported hardware validation claim found: {claim}")
    if "NOT RUN" not in hardware_text:
        fail("hardware validation matrix must still mark unrun rows honestly")

    print("check_hil_contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

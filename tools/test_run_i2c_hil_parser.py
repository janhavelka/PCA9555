#!/usr/bin/env python3
"""Host tests for PCA9555 HIL runner command parsing/classification."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import re
import sys
import tempfile
from collections.abc import Callable


ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools" / "run_i2c_hil.py"


def load_runner():
    spec = importlib.util.spec_from_file_location("run_i2c_hil_under_test", RUNNER)
    if spec is None or spec.loader is None:
        raise AssertionError("could not load run_i2c_hil.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


runner = load_runner()


def assert_true(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def assert_equal(actual, expected, message: str) -> None:
    if actual != expected:
        raise AssertionError(f"{message}: {actual!r} != {expected!r}")


def test_default_safe_command_sequence() -> None:
    specs = list(runner.DEFAULT_SAFE_COMMANDS)
    commands = [spec.command for spec in specs]

    for command in ("version", "scan", "probe", "settings", "health"):
        assert_true(command in commands, f"default sequence missing {command}")
    assert_equal(commands.count("health"), 2, "default sequence should capture health before and after stress")

    unsafe = [
        spec.command
        for spec in specs
        if spec.destructive or spec.requires_opt_in or spec.operator_check or runner.command_is_unsafe(spec.command)
    ]
    assert_equal(unsafe, [], "default safe commands must not mutate outputs or require opt-in")
    for spec in specs:
        assert_true(spec.timeout_s > 0.0, f"{spec.command} timeout must be positive")


def test_address_scan_is_not_identity_proof() -> None:
    args = runner.parse_args(["--dry-run", "--address", "0x24"])
    specs = runner.build_command_sequence(args)
    scan = next(spec for spec in specs if spec.command == "scan")
    probe = next(spec for spec in specs if spec.command == "probe")

    assert_true("ACK" in scan.purpose or "acknowledged" in scan.notes, "scan must be described as ACK-only")
    assert_true("no documented chip ID" in scan.notes, "scan notes must reject identity proof")
    assert_true("does not prove chip identity" in probe.notes, "probe notes must reject identity proof")
    assert_true(
        any(re.search(pattern, "24: 20 24 27", flags=re.IGNORECASE) for pattern in scan.expected),
        "scan expected regex must include configured address token",
    )


def test_failure_token_classification() -> None:
    spec = runner.CommandSpec(
        command="probe",
        purpose="test probe",
        expected=(r"Status:\s+OK",),
    )

    result, evidence = runner.classify(spec, "Status: OK\n", "prompt")
    assert_equal(result, "PASS", "OK status should pass")
    assert_true(evidence, "PASS should include concise evidence")

    for text in (
        "Status: I2C_TIMEOUT\n",
        "[FAIL] probe responds\n",
        "Stress result: fail=1\n",
        "Summary: failed=2\n",
        "[E] Usage: bad command\n",
    ):
        result, evidence = runner.classify(spec, text, "prompt")
        assert_equal(result, "FAIL", f"failure token should fail for {text!r}")
        assert_true(evidence, "FAIL should include evidence")

    result, _ = runner.classify(spec, "Status: OK\n", "timeout")
    assert_equal(result, "TIMEOUT", "timeout completion reason must dominate transcript text")


def test_destructive_command_gating() -> None:
    safe_args = runner.parse_args(["--dry-run"])
    opt_in_args = runner.parse_args(["--dry-run", "--include-output-tests"])

    for command in (
        "allhigh",
        "pattern 0xAAAA",
        "wreg 2 0x00",
        "wregs 2 0xAA 0x55",
        "dirout 0x0001",
        "stress_mix 1",
        "recover",
    ):
        spec = runner.custom_command_spec(command, 5.0)
        assert_true(spec.destructive, f"{command} must be classified destructive")
        assert_equal(
            spec.requires_opt_in,
            "--include-output-tests",
            f"{command} must require output-test opt-in",
        )
        assert_true(not runner.opt_in_enabled(spec, safe_args), f"{command} must be gated by default")
        assert_true(runner.opt_in_enabled(spec, opt_in_args), f"{command} must run after explicit opt-in")


def test_json_destructive_command_requires_opt_in() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / "commands.json"
        path.write_text(
            json.dumps([
                {
                    "command": "custom-safe-name",
                    "destructive": True,
                    "purpose": "Operator-declared destructive command.",
                }
            ]),
            encoding="utf-8",
        )
        specs = runner.load_command_file(path, 5.0)

    assert_equal(len(specs), 1, "JSON command file should produce one spec")
    assert_true(specs[0].destructive, "JSON destructive flag must be preserved")
    assert_equal(
        specs[0].requires_opt_in,
        "--include-output-tests",
        "operator-declared destructive command must require explicit opt-in",
    )


def test_custom_read_only_stress_soak_gating() -> None:
    safe_args = runner.parse_args(["--dry-run"])
    soak_args = runner.parse_args(["--dry-run", "--include-soak"])

    for command in ("stress", "stress 10"):
        spec = runner.custom_command_spec(command, 5.0)
        assert_true(not spec.destructive, f"{command} should remain read-only")
        assert_equal(spec.requires_opt_in, None, f"{command} should stay in the safe stress budget")
        assert_true(runner.opt_in_enabled(spec, safe_args), f"{command} should run by default")

    spec = runner.custom_command_spec("stress 11", 5.0)
    assert_true(not spec.destructive, "read-only long stress should not be destructive")
    assert_equal(spec.requires_opt_in, "--include-soak", "long stress must require soak opt-in")
    assert_true(not runner.opt_in_enabled(spec, safe_args), "long stress must be gated by default")
    assert_true(runner.opt_in_enabled(spec, soak_args), "long stress should run after soak opt-in")

    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / "commands.json"
        path.write_text(json.dumps([{"command": "stress 1000"}]), encoding="utf-8")
        specs = runner.load_command_file(path, 5.0)
    assert_equal(specs[0].requires_opt_in, "--include-soak", "JSON long stress must require soak opt-in")


def test_read_only_custom_commands_remain_reviewable_not_destructive() -> None:
    for command in ("version", "scan", "probe", "settings", "health", "read", "dump"):
        spec = runner.custom_command_spec(command, 5.0)
        assert_true(not spec.destructive, f"{command} should not be destructive")
        assert_equal(spec.requires_opt_in, None, f"{command} should not require output-test opt-in")
        assert_equal(spec.timeout_s, 5.0, f"{command} should keep requested timeout")


def main() -> int:
    tests: tuple[Callable[[], None], ...] = (
        test_default_safe_command_sequence,
        test_address_scan_is_not_identity_proof,
        test_failure_token_classification,
        test_destructive_command_gating,
        test_json_destructive_command_requires_opt_in,
        test_custom_read_only_stress_soak_gating,
        test_read_only_custom_commands_remain_reviewable_not_destructive,
    )
    for test in tests:
        test()
        print(f"{test.__name__}: PASS")
    print("test_run_i2c_hil_parser: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

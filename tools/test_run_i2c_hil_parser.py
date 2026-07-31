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


def test_prompt_gated_long_commands_do_not_use_early_completion_tokens() -> None:
    specs = {
        spec.command: spec
        for spec in (*runner.DEFAULT_SAFE_COMMANDS, *runner.OPTIONAL_COMMANDS)
    }

    for command in (
        "stress 10",
        "stress 1000",
        "stress_mix 100 confirm",
        "selftest confirm",
    ):
        assert_equal(
            specs[command].completion_tokens,
            (),
            f"{command} must wait for the CLI prompt before the next command",
        )


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
        expected_recovery = None if command == "recover" else "recover confirm"
        assert_equal(
            spec.recovery_command,
            expected_recovery,
            f"{command} recovery metadata should be safe and nonrecursive",
        )


def test_dynamic_full_function_commands_have_expected_pass_patterns() -> None:
    cases = (
        ("stress 100", "=== Stress Results ===\nTotal: ok=100 fail=0", "stress"),
        ("allhigh confirm", "All 16 pins set to OUTPUT HIGH", "output_pattern"),
        ("alllow confirm", "All 16 pins set to OUTPUT LOW", "output_pattern"),
        ("pattern 0xAAAA confirm", "Pattern applied: value=0xAAAA", "output_pattern"),
        ("walk 0 confirm", "=== Walking-1 Test\n16 passed\n0 failed", "output_pattern"),
        ("sweep 0 confirm", "=== Sweep Test\n32 passed\n0 failed", "output_pattern"),
        ("setbits 0x0003 confirm", "Output latch bits set HIGH: mask=0x0003", "mask_write"),
        ("clearbits 0x0003 confirm", "Output latch bits cleared LOW: mask=0x0003", "mask_write"),
        ("togglebits 0x0101 confirm", "Output latch bits toggled: mask=0x0101", "mask_write"),
        ("dirout 0x000F confirm", "Pins configured as OUTPUT: mask=0x000F", "direction"),
        ("dirin 0x000F confirm", "Pins configured as INPUT: mask=0x000F", "direction"),
        ("invertset 0x000F confirm", "Polarity inversion enabled: mask=0x000F", "polarity"),
        ("invertclr 0x000F confirm", "Polarity inversion disabled: mask=0x000F", "polarity"),
        ("wpin 0 1 confirm", "Output latch pin 0 (P00) = 1", "pin_write"),
        ("toggle 0 confirm", "Output latch pin 0 (P00) toggled", "pin_write"),
        ("dir 0 out confirm", "Pin 0 (P00) set to OUTPUT", "direction"),
        ("wport 0 0xAA confirm", "Port 0 output latch set to 0xAA", "port_write"),
        ("dport 0 0x00 confirm", "Port 0 config set to 0x00", "direction"),
        ("pol 0 1 confirm", "Pin 0 (P00) polarity set to INVERTED", "polarity"),
        ("wpol 0 0x00 confirm", "Port 0 polarity set to 0x00", "polarity"),
        ("rreg 2", "Reg 0x02 = 0xAA", "register_read"),
        ("rregs 2 2", "Regs 0x02=0xAA 0x03=0x55", "register_read"),
        ("wreg 2 0xAA confirm", "Reg 0x02 set to 0xAA", "register_write"),
        ("wregs 2 0xAA 0x55 confirm", "Regs 0x02=0xAA 0x03=0x55", "register_write"),
        ("recover confirm", "Attempting recovery...\nStatus: OK", "recovery"),
    )
    opt_in_args = runner.parse_args(["--dry-run", "--include-output-tests"])

    for command, transcript, classifier in cases:
        spec = runner.spec_for_command(command, 5.0)
        assert_equal(spec.classifier, classifier, f"{command} classifier should be specific")
        if spec.destructive:
            assert_equal(spec.requires_opt_in, "--include-output-tests", f"{command} should be opt-in gated")
            assert_true(runner.opt_in_enabled(spec, opt_in_args), f"{command} should run after opt-in")
            expected_recovery = None if command == "recover confirm" else "recover confirm"
            assert_equal(spec.recovery_command, expected_recovery, f"{command} recovery metadata")
        result, evidence = runner.classify(spec, transcript, "prompt")
        assert_equal(result, "PASS", f"{command} expected transcript should pass")
        assert_true(evidence, f"{command} PASS should include evidence")


def test_raw_write_dynamic_specs_match_driver_range() -> None:
    for start in ("2", "3", "4", "5", "0x02", "0x05"):
        for command in (
            f"wreg {start} 0xAA confirm",
            f"wregs {start} 0xAA 0x55 confirm",
        ):
            spec = runner.dynamic_cli_command_spec(command, 5.0)
            assert_true(spec is not None, f"{command} should have a dynamic spec")
            assert_equal(spec.classifier, "register_write", f"{command} classifier")
            assert_true(spec.destructive, f"{command} must remain output-test gated")

    for start in ("6", "7", "0x06", "0x07"):
        for command in (
            f"wreg {start} 0xFF confirm",
            f"wregs {start} 0xFF 0xFF confirm",
        ):
            spec = runner.dynamic_cli_command_spec(command, 5.0)
            assert_true(
                spec is None or spec.classifier != "register_write",
                f"{command} must not be classified as a successful raw write",
            )


def test_fault_guard_plan_is_explicit_and_opt_in() -> None:
    safe_args = runner.parse_args(["--dry-run"])
    fault_args = runner.parse_args(["--dry-run", "--include-fault-tests"])
    specs = list(runner.FAULT_GUARD_COMMANDS)

    assert_true(specs, "fault flag must have a nonempty command plan")
    assert_equal(specs[0].classifier, "fault_health_before", "fault plan health baseline")
    assert_equal(specs[1].classifier, "fault_settings_before", "fault plan settings baseline")
    assert_equal(specs[-2].classifier, "fault_settings_after", "fault plan settings final")
    assert_equal(specs[-1].classifier, "fault_health_after", "fault plan health final")
    for spec in specs:
        assert_equal(spec.requires_opt_in, "--include-fault-tests", f"{spec.command} fault opt-in")
        assert_true(not spec.destructive, f"{spec.command} fault guard must intend no mutation")
        assert_equal(spec.recovery_command, None, f"{spec.command} must not trigger recovery I2C")
        assert_true(not runner.opt_in_enabled(spec, safe_args), f"{spec.command} must be gated")
        assert_true(runner.opt_in_enabled(spec, fault_args), f"{spec.command} must run after opt-in")

    rejection_specs = specs[2:-2]
    assert_true(rejection_specs, "fault plan must contain expected rejection cases")
    for spec in rejection_specs:
        assert_true(spec.expected_rejection, f"{spec.command} must be an exact rejection")
        assert_true(spec.expected, f"{spec.command} must require exact evidence")
        if spec.command.endswith(" confirm"):
            assert_true(
                spec.command in {
                    "wreg 6 0xFF confirm",
                    "wregs 7 0xFF 0xFF confirm",
                },
                f"{spec.command} is not a reviewed double-guarded Configuration rejection",
            )


def test_expected_rejection_classification_is_fail_closed() -> None:
    spec = next(
        item for item in runner.FAULT_GUARD_COMMANDS
        if item.command == "rpin 16"
    )
    transcript = "[E] Usage: rpin <pin 0-15>\n"
    result, evidence = runner.classify(spec, transcript, "prompt")
    assert_equal(result, "PASS", "the exact reviewed rejection should pass")
    assert_true(evidence, "expected rejection PASS should retain evidence")

    result, _ = runner.classify(spec, "[E] Usage: another command\n", "prompt")
    assert_true(result != "PASS", "a different rejection must not pass")
    result, _ = runner.classify(spec, transcript + "Status: I2C_NACK_ADDR\n", "prompt")
    assert_equal(result, "FAIL", "a transport error must dominate expected rejection")
    result, _ = runner.classify(spec, transcript + "[FAIL] unexpected I2C\n", "prompt")
    assert_equal(result, "FAIL", "a hard failure must dominate expected rejection")
    result, _ = runner.classify(
        spec,
        transcript + "[W] Unknown command: 'unrelated'\n",
        "prompt",
    )
    assert_equal(result, "FAIL", "an unrelated soft failure must not be hidden")
    result, _ = runner.classify(
        spec,
        transcript + "[E] Usage: another command\n",
        "prompt",
    )
    assert_equal(result, "FAIL", "an additional Usage failure must not be hidden")
    result, _ = runner.classify(spec, transcript, "timeout")
    assert_equal(result, "TIMEOUT", "timeout must dominate expected rejection")


def make_fault_snapshot_results(
    *, total_success_after: int = 42, timeout_after_ms: int = 50,
    uncertain_after: str = "0x00"
) -> list:
    def result(classifier: str, evidence: list[str]):
        return runner.CommandResult(
            command="health" if "health" in classifier else "settings",
            purpose="fault snapshot",
            classifier=classifier,
            serial_result="PASS",
            operator_result="N/A",
            completion_reason="prompt",
            elapsed_s=0.0,
            notes="",
            evidence=evidence,
        )

    health_before = [
        "=== Driver Health ===",
        "State: READY Bound: YES Consecutive failures: 0 Total success: 42 Total failures: 0 Last error: never",
    ]
    health_after = [
        "=== Driver Health ===",
        f"State: READY Bound: YES Consecutive failures: 0 Total success: {total_success_after} Total failures: 0 Last error: never",
    ]
    settings_before = [
        "=== Settings Snapshot ===",
        "Initialized: YES",
        "State: READY",
        "I2C address: 0x20",
        "Timeout: 50 ms",
        "Interrupt errata workaround: mandatory",
        "nowMs hook: SET",
        "Shadow valid pairs: 0x07",
        "Uncertain pairs: 0x00",
    ]
    settings_after = [
        *settings_before[:4],
        f"Timeout: {timeout_after_ms} ms",
        *settings_before[5:-1],
        f"Uncertain pairs: {uncertain_after}",
    ]
    return [
        result("fault_health_before", health_before),
        result("fault_settings_before", settings_before),
        result("fault_settings_after", settings_after),
        result("fault_health_after", health_after),
    ]


def test_fault_guard_snapshot_invariant() -> None:
    unchanged = runner.fault_guard_invariant_result(make_fault_snapshot_results())
    assert_equal(unchanged.serial_result, "PASS", "unchanged fault snapshots must pass")

    counter_changed = runner.fault_guard_invariant_result(
        make_fault_snapshot_results(total_success_after=43)
    )
    assert_equal(counter_changed.serial_result, "FAIL", "tracked I2C must fail the invariant")

    shadow_changed = runner.fault_guard_invariant_result(
        make_fault_snapshot_results(uncertain_after="0x01")
    )
    assert_equal(shadow_changed.serial_result, "FAIL", "shadow changes must fail the invariant")

    settings_changed = runner.fault_guard_invariant_result(
        make_fault_snapshot_results(timeout_after_ms=51)
    )
    assert_equal(settings_changed.serial_result, "FAIL", "settings changes must fail the invariant")

    missing = runner.fault_guard_invariant_result(make_fault_snapshot_results()[:-1])
    assert_equal(missing.serial_result, "FAIL", "missing snapshots must fail the invariant")


def test_text_command_file_uses_dynamic_specs() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / "commands.txt"
        path.write_text("allhigh confirm\nrreg 2\n", encoding="utf-8")
        specs = runner.load_command_file(path, 5.0)

    assert_equal(specs[0].classifier, "output_pattern", "text allhigh should use dynamic spec")
    assert_equal(specs[0].expected, (r"All 16 pins set to OUTPUT HIGH",), "text allhigh expected pattern")
    assert_equal(specs[1].classifier, "register_read", "text rreg should use dynamic spec")
    assert_true(specs[0].destructive, "text allhigh should remain destructive")


def test_recovery_spec_uses_dynamic_expected_patterns() -> None:
    selftest = next(spec for spec in runner.OPTIONAL_COMMANDS if spec.command == "selftest confirm")
    recovery = runner.recovery_spec_for(selftest)

    assert_true(recovery is not None, "selftest should have recovery spec")
    assert_equal(recovery.command, "recover confirm", "selftest recovery command")
    assert_equal(recovery.classifier, "recovery", "recovery should use dynamic recovery classifier")
    result, evidence = runner.classify(
        recovery,
        "Attempting recovery...\nStatus: OK\n",
        "prompt",
    )
    assert_equal(result, "PASS", "complete-image recovery transcript should pass")
    assert_true(evidence, "recovery PASS should include evidence")


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


def test_timeout_aliases_and_override() -> None:
    args = runner.parse_args([
        "--dry-run",
        "--timeout-s",
        "7",
        "--idle-timeout-s",
        "0.25",
        "--boot-settle-s",
        "1.5",
        "--serial-reopen-interval-s",
        "1800",
        "--allow-idle-completion",
    ])
    assert_equal(args.timeout, 7.0, "--timeout-s alias should set timeout")
    assert_equal(args.idle_gap, 0.25, "--idle-timeout-s alias should set idle gap")
    assert_equal(args.boot_settle_s, 1.5, "--boot-settle-s should parse")
    assert_equal(args.serial_reopen_interval_s, 1800.0, "--serial-reopen-interval-s should parse")
    assert_equal(args.serial_dtr, "0", "--serial-dtr should deassert by default")
    assert_equal(args.serial_rts, "0", "--serial-rts should deassert by default")
    assert_true(args.allow_idle_completion, "--allow-idle-completion should parse")

    line_args = runner.parse_args([
        "--dry-run",
        "--serial-dtr",
        "keep",
        "--serial-rts",
        "0",
    ])
    assert_equal(line_args.serial_dtr, "keep", "--serial-dtr keep should parse")
    assert_equal(line_args.serial_rts, "0", "--serial-rts 0 should parse")

    specs = runner.build_command_sequence(args)
    runnable = [spec for spec in specs if spec.command in ("version", "scan", "stress 10")]
    assert_true(runnable, "default sequence should contain checked commands")
    original_timeouts = {spec.command: spec.timeout_s for spec in runner.DEFAULT_SAFE_COMMANDS}
    for spec in runnable:
        assert_equal(
            spec.timeout_s,
            max(7.0, original_timeouts[spec.command]),
            f"{spec.command} timeout override must not shorten its safe bound",
        )


def test_parser_self_test_entrypoint() -> None:
    assert_equal(runner.run_parser_self_test(), 0, "parser self-test should pass")


def make_aggregate_stats(result_counts: dict[str, int], failures: int = 0):
    return runner.AggregateStats(
        label="soak",
        command_counts={"read": sum(result_counts.values())},
        result_counts=result_counts,
        started_at="2026-06-23T00:00:00",
        ended_at="2026-06-23T00:00:01",
        elapsed_s=1.0,
        completed=sum(result_counts.values()),
        failures=failures,
        min_latency_s=0.1,
        mean_latency_s=0.1,
        max_latency_s=0.1,
        effective_hz=1.0,
        stop_reason="duration_limit",
    )


def test_aggregate_result_flags_review_counts() -> None:
    result = runner.aggregate_result(
        make_aggregate_stats({"PASS": 1, "SERIAL_OK_OR_REVIEW": 1})
    )
    assert_equal(
        result.serial_result,
        "SERIAL_OK_OR_REVIEW",
        "aggregate must not PASS review-classified serial captures",
    )
    assert_true(
        any("SERIAL_OK_OR_REVIEW" in item for item in result.evidence),
        "aggregate evidence should include result_counts",
    )


def test_aggregate_result_flags_fail_counts() -> None:
    result = runner.aggregate_result(make_aggregate_stats({"PASS": 1, "TIMEOUT": 1}, failures=1))
    assert_equal(result.serial_result, "FAIL", "aggregate must fail when command failures exist")


def test_aggregate_destructive_failure_runs_recovery_before_stopping() -> None:
    calls: list[str] = []
    original = runner.run_serial_command

    def fake_run_serial_command(_ser, spec, **_kwargs):
        calls.append(spec.command)
        failed = spec.command == "allhigh confirm"
        return (
            runner.CommandResult(
                command=spec.command,
                purpose=spec.purpose,
                classifier=spec.classifier,
                serial_result="FAIL" if failed else "PASS",
                operator_result="N/A",
                completion_reason="prompt",
                elapsed_s=0.001,
                notes=spec.notes,
                evidence=[],
            ),
            "",
        )

    class FakeSerial:
        def reset_input_buffer(self) -> None:
            pass

    runner.run_serial_command = fake_run_serial_command
    try:
        spec = runner.dynamic_cli_command_spec("allhigh confirm", 5.0)
        assert_true(spec is not None, "allhigh dynamic spec must exist")
        stats, results, _ = runner.run_aggregate_commands(
            FakeSerial(),
            [spec],
            label="soak",
            max_commands=1,
            deadline_s=None,
            prompt=runner.PROMPT_TOKEN,
            idle_gap_s=0.1,
            allow_idle_completion=False,
            interval_s=0.0,
            failure_limit=1,
            verbose=False,
        )
    finally:
        runner.run_serial_command = original

    assert_equal(calls, ["allhigh confirm", "recover confirm"], "recovery must run before stop")
    assert_equal(len(results), 2, "destructive step and recovery must both be retained")
    assert_equal(stats.completed, 2, "aggregate stats must count the recovery attempt")
    assert_equal(stats.failures, 1, "the primary destructive failure must remain visible")
    assert_equal(stats.stop_reason, "failure_limit", "failure limit should apply after recovery")
    assert_equal(stats.first_anomaly_command, "allhigh confirm", "first anomaly command")
    assert_equal(stats.first_anomaly_result, "FAIL", "first anomaly result")


def test_fail_verdict_maps_to_nonzero_exit() -> None:
    assert_equal(runner.exit_code_for_verdict("FAIL"), 1, "FAIL verdict must fail the process")
    assert_equal(runner.exit_code_for_verdict("OPERATOR_REVIEW_REQUIRED"), 0, "manual-review verdict remains usable")


def make_result(serial_result: str):
    return runner.CommandResult(
        command=serial_result.lower(),
        purpose="test",
        classifier="test",
        serial_result=serial_result,
        operator_result="N/A",
        completion_reason="prompt",
        elapsed_s=0.0,
        notes="",
        evidence=[],
    )


def test_serial_anomaly_maps_to_nonzero_exit_without_failing_manual_rows() -> None:
    assert_equal(
        runner.exit_code_for_results([make_result("SERIAL_OK_OR_REVIEW")], False),
        1,
        "serial review captures must fail the process for unattended gates",
    )
    assert_equal(
        runner.exit_code_for_results([make_result("OPERATOR_CHECK_REQUIRED")], False),
        0,
        "manual operator rows alone should not fail the runner process",
    )
    assert_equal(
        runner.exit_code_for_results([make_result("SKIPPED_UNSAFE")], False),
        0,
        "skipped unsafe opt-in commands should not fail read-only HIL",
    )
    assert_true(
        not runner.command_result_is_serial_anomaly(make_result("SKIPPED_UNSAFE")),
        "skipped unsafe rows should not block aggregate phases",
    )
    assert_true(
        runner.command_result_is_serial_anomaly(make_result("SERIAL_OK_OR_REVIEW")),
        "review-classified serial rows should block aggregate phases",
    )


def test_dry_run_artifacts_include_classifier_and_timing_options() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        rc = runner.run([
            "--dry-run",
            "--out",
            tmp,
            "--timeout-s",
            "6",
            "--idle-timeout-s",
            "0.3",
            "--boot-settle-s",
            "0.1",
        ])
        assert_equal(rc, 0, "dry-run should exit successfully")
        log_dirs = list(pathlib.Path(tmp).glob("i2c_*"))
        assert_equal(len(log_dirs), 1, "dry-run should create one log directory")
        summary = json.loads((log_dirs[0] / "summary.json").read_text(encoding="utf-8"))
        assert_equal(summary["timeout_minimum_s"], 6.0, "summary should record timeout minimum")
        assert_equal(summary["idle_timeout_s"], 0.3, "summary should record idle timeout")
        assert_equal(summary["boot_settle_s"], 0.1, "summary should record boot settle")
        assert_equal(summary["serial_reopen_interval_s"], 0.0, "summary should record serial reopen interval")
        assert_equal(summary["serial_dtr"], "0", "summary should record serial DTR setting")
        assert_equal(summary["serial_rts"], "0", "summary should record serial RTS setting")
        assert_equal(summary["allow_idle_completion"], False, "idle completion should be off by default")
        assert_true(
            all("classifier" in command for command in summary["commands"]),
            "summary command rows should include classifier",
        )
        summary_md = (log_dirs[0] / "summary.md").read_text(encoding="utf-8")
        assert_true("## Results" in summary_md, "summary should include condensed results")
        assert_true("Raw CLI output is not retained" in summary_md, "summary should document bounded evidence")
        assert_true(
            not (log_dirs[0] / "serial_transcript.txt").exists(),
            "dry-run should not create a serial transcript",
        )
        assert_true(
            not (log_dirs[0] / "transcripts").exists(),
            "dry-run should not create per-command transcripts",
        )


def test_fault_flag_adds_real_commands_to_dry_run() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        rc = runner.run([
            "--dry-run",
            "--include-fault-tests",
            "--out",
            tmp,
        ])
        assert_equal(rc, 0, "fault-plan dry-run should exit successfully")
        log_dir = next(pathlib.Path(tmp).glob("i2c_*"))
        summary = json.loads((log_dir / "summary.json").read_text(encoding="utf-8"))
        commands = [item["command"] for item in summary["commands"]]
        for spec in runner.FAULT_GUARD_COMMANDS:
            assert_true(spec.command in commands, f"fault dry-run missing {spec.command}")


class ZeroWaitingSerial:
    in_waiting = 0

    def __init__(self, chunks: tuple[bytes, ...]) -> None:
        self._chunks = list(chunks)

    def read(self, _size: int) -> bytes:
        if not self._chunks:
            return b""
        return self._chunks.pop(0)


class DeferredOpenSerial:
    def __init__(self) -> None:
        self.events: list[tuple[str, object]] = []
        self.port = None
        self.baudrate = None
        self.timeout = None
        self._dtr = True
        self._rts = True

    @property
    def dtr(self) -> bool:
        return self._dtr

    @dtr.setter
    def dtr(self, value: bool) -> None:
        self._dtr = value
        self.events.append(("dtr", value))

    @property
    def rts(self) -> bool:
        return self._rts

    @rts.setter
    def rts(self, value: bool) -> None:
        self._rts = value
        self.events.append(("rts", value))

    def open(self) -> None:
        self.events.append(("open", None))


class DeferredSerialModule:
    def __init__(self) -> None:
        self.instance = DeferredOpenSerial()

    def Serial(self, *args, **kwargs):
        assert_equal(args, (), "serial port must not be opened by the constructor")
        assert_equal(kwargs, {}, "serial port must be configured before explicit open")
        return self.instance


def test_serial_lines_are_configured_before_open() -> None:
    serial_mod = DeferredSerialModule()
    args = runner.parse_args(["--port", "COM4", "--baud", "230400"])
    ser = runner.open_serial_with_retries(serial_mod, args)
    assert_equal(ser.port, "COM4", "serial port should be assigned before open")
    assert_equal(ser.baudrate, 230400, "serial baud should be assigned before open")
    assert_equal(ser.timeout, 0.05, "serial read timeout should be bounded")
    assert_equal(
        ser.events,
        [("dtr", False), ("rts", False), ("open", None)],
        "DTR and RTS must be deasserted before opening native USB",
    )


class StartupSyncSerial(ZeroWaitingSerial):
    def __init__(self, chunks: tuple[bytes, ...]) -> None:
        super().__init__(chunks)
        self.written = b""
        self.flushed = False

    def write(self, data: bytes) -> int:
        self.written += data
        return len(data)

    def flush(self) -> None:
        self.flushed = True


def test_startup_sync_uses_read_only_health_framing() -> None:
    ser = StartupSyncSerial((b"stale tail\n> ", b"=== Driver Health ===\n", b"State: READY\n> "))
    text, reason = runner.synchronize_cli(
        ser,
        prompt="> ",
        timeout_s=1.0,
        idle_gap_s=0.1,
    )
    assert_equal(ser.written, b"\nhealth\n", "startup sync should terminate stale input and request health")
    assert_true(ser.flushed, "startup sync command should be flushed")
    assert_equal(reason, "prompt", "startup sync should require health evidence followed by the prompt")
    assert_true("=== Driver Health ===" in text, "startup sync should capture its read-only marker")


def test_read_until_does_not_depend_on_in_waiting() -> None:
    ser = ZeroWaitingSerial((b"help token <P> ", b"continued\n", b"Status: OK\n", b"> "))
    text, reason = runner.read_until(
        ser,
        prompt="> ",
        timeout_s=1.0,
        idle_gap_s=0.1,
    )
    assert_equal(reason, "prompt", "prompt should be detected even when in_waiting stays zero")
    assert_true("continued" in text, "inline command syntax must not be treated as a prompt")
    assert_true("Status: OK" in text, "read_until should capture data returned by bounded reads")


def test_read_until_ignores_stale_prompt_until_current_evidence() -> None:
    ser = ZeroWaitingSerial((
        b"delayed tail\n> ",
        b"=== PCA9555 selftest ===\n",
        b"Selftest result: pass=50 fail=0 skip=0\n",
    ))
    text, reason = runner.read_until(
        ser,
        prompt="> ",
        timeout_s=1.0,
        idle_gap_s=0.05,
        expected_patterns=(r"Selftest result:", r"fail=0"),
    )
    assert_equal(
        reason,
        "expected_evidence",
        "conclusive current-command evidence should tolerate a delayed prompt tail",
    )
    assert_true("pass=50" in text, "the stale prompt must not terminate the current response")


def test_failure_text_does_not_end_an_active_command_without_prompt() -> None:
    ser = ZeroWaitingSerial((b"[FAIL] step 1\n",))
    _text, reason = runner.read_until(
        ser,
        prompt="> ",
        timeout_s=0.1,
        idle_gap_s=0.02,
        expected_patterns=(r"32 passed", r"0 failed"),
    )
    assert_equal(
        reason,
        "timeout",
        "failure text alone must not end a command that may still be mutating hardware",
    )


def main() -> int:
    tests: tuple[Callable[[], None], ...] = (
        test_default_safe_command_sequence,
        test_prompt_gated_long_commands_do_not_use_early_completion_tokens,
        test_address_scan_is_not_identity_proof,
        test_failure_token_classification,
        test_destructive_command_gating,
        test_dynamic_full_function_commands_have_expected_pass_patterns,
        test_raw_write_dynamic_specs_match_driver_range,
        test_fault_guard_plan_is_explicit_and_opt_in,
        test_expected_rejection_classification_is_fail_closed,
        test_fault_guard_snapshot_invariant,
        test_text_command_file_uses_dynamic_specs,
        test_recovery_spec_uses_dynamic_expected_patterns,
        test_json_destructive_command_requires_opt_in,
        test_custom_read_only_stress_soak_gating,
        test_read_only_custom_commands_remain_reviewable_not_destructive,
        test_timeout_aliases_and_override,
        test_parser_self_test_entrypoint,
        test_aggregate_result_flags_review_counts,
        test_aggregate_result_flags_fail_counts,
        test_aggregate_destructive_failure_runs_recovery_before_stopping,
        test_fail_verdict_maps_to_nonzero_exit,
        test_serial_anomaly_maps_to_nonzero_exit_without_failing_manual_rows,
        test_dry_run_artifacts_include_classifier_and_timing_options,
        test_fault_flag_adds_real_commands_to_dry_run,
        test_serial_lines_are_configured_before_open,
        test_startup_sync_uses_read_only_health_framing,
        test_read_until_does_not_depend_on_in_waiting,
        test_read_until_ignores_stale_prompt_until_current_evidence,
        test_failure_text_does_not_end_an_active_command_without_prompt,
    )
    for test in tests:
        test()
        print(f"{test.__name__}: PASS")
    print("test_run_i2c_hil_parser: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

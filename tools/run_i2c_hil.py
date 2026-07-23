#!/usr/bin/env python3
"""Host-side PCA9555 serial CLI HIL runner.

The runner drives the existing Arduino bring-up CLI over a serial port. It does
not flash firmware, it does not prove hardware identity from an I2C ACK, and it
does not run output-changing commands unless the operator opts in.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as _dt
import json
import pathlib
import re
import subprocess
import shutil
import sys
import time
from typing import Iterable


ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
PROMPT_TOKEN = "> "
PYSERIAL_HINT = "python -m pip install pyserial"
DEFAULT_ADDRESS = "0x20"
DEFAULT_SAFE_STRESS_CYCLES = 10
READ_STRESS_COMMAND_RE = re.compile(r"^\s*stress(?:\s+([0-9]+))?\s*$", re.IGNORECASE)
UNSAFE_COMMAND_RE = re.compile(
    r"^\s*(?:"
    r"write\s+pin|wpin|toggle(?:\s|$)|dir(?:\s|$)|dir\s+pin|write\s+port|wport|"
    r"dir\s+port|dport|polarity\s+pin|pol(?:\s|$)|polarity\s+port|wpol|"
    r"setbits|sb(?:\s|$)|clearbits|cb(?:\s|$)|togglebits|tb(?:\s|$)|"
    r"dirin|dirout|invertset|invertclr|write\s+reg|wreg|write\s+regs|wregs|"
    r"pattern|pat(?:\s|$)|allhigh|alllow|sweep|walk|selftest|stress_mix|recover"
    r")",
    re.IGNORECASE,
)


@dataclasses.dataclass(frozen=True)
class CommandSpec:
    """One serial command plus auditor-facing metadata."""

    command: str
    purpose: str
    expected: tuple[str, ...] = ()
    timeout_s: float = 5.0
    completion_tokens: tuple[str, ...] = ()
    operator_check: bool = False
    destructive: bool = False
    requires_opt_in: str | None = None
    recovery_command: str | None = None
    notes: str = ""
    classifier: str = "generic"

@dataclasses.dataclass
class CommandResult:
    command: str
    purpose: str
    classifier: str
    serial_result: str
    operator_result: str
    completion_reason: str
    elapsed_s: float
    notes: str
    evidence: list[str]


@dataclasses.dataclass
class AggregateStats:
    """Bounded repeated-command run statistics."""

    label: str
    command_counts: dict[str, int]
    result_counts: dict[str, int]
    started_at: str
    ended_at: str
    elapsed_s: float
    completed: int
    failures: int
    min_latency_s: float | None
    mean_latency_s: float | None
    max_latency_s: float | None
    effective_hz: float
    stop_reason: str
    serial_reopens: int = 0
    first_anomaly_command: str | None = None
    first_anomaly_result: str | None = None
    first_anomaly_reason: str | None = None
    first_anomaly_evidence: list[str] = dataclasses.field(default_factory=list)


DEFAULT_SAFE_COMMANDS: tuple[CommandSpec, ...] = (
    CommandSpec(
        command="version",
        purpose="Print firmware and library version information.",
        expected=(r"=== Version Info ===", r"PCA9555 library version"),
        timeout_s=3.0,
        classifier="section",
    ),
    CommandSpec(
        command="help",
        purpose="Verify the CLI command surface.",
        expected=(r"PCA9555 CLI Help",),
        timeout_s=3.0,
        classifier="section",
    ),
    CommandSpec(
        command="scan",
        purpose="Scan I2C addresses; ACK proves address response only.",
        expected=(r"Scan complete", r"20"),
        timeout_s=15.0,
        classifier="scan",
        notes=(
            "A scan proves only that an address acknowledged. PCA9555 has no "
            "documented chip ID register."
        ),
    ),
    CommandSpec(
        command="probe",
        purpose="Run the driver raw address probe without health tracking.",
        expected=(r"Status:\s+OK",),
        timeout_s=5.0,
        classifier="probe",
        notes="Probe is ACK-only and does not prove chip identity.",
    ),
    CommandSpec(
        command="settings",
        purpose="Capture active driver settings.",
        expected=(r"=== Settings Snapshot ===", r"I2C address"),
        timeout_s=3.0,
        classifier="section",
    ),
    CommandSpec(
        command="read",
        purpose="Read both input ports; clears PCA9555 input interrupt state.",
        expected=(r"=== Input Ports ===", r"Combined:\s+0x"),
        timeout_s=5.0,
        classifier="read",
        notes="Input reads clear interrupt sources and apply the errata pointer-park write.",
    ),
    CommandSpec(
        command="outputs",
        purpose="Read output latch registers.",
        expected=(r"=== Output Ports ===", r"Combined:\s+0x"),
        timeout_s=5.0,
        classifier="read",
    ),
    CommandSpec(
        command="config",
        purpose="Read configuration registers.",
        expected=(r"=== Configuration \(1=input, 0=output\) ===",),
        timeout_s=5.0,
        classifier="read",
    ),
    CommandSpec(
        command="polarity",
        purpose="Read polarity inversion registers.",
        expected=(r"=== Polarity Inversion \(1=inverted\) ===",),
        timeout_s=5.0,
        classifier="read",
    ),
    CommandSpec(
        command="dump",
        purpose="Capture all PCA9555 register pairs exposed by the CLI.",
        expected=(r"=== Register Dump ===",),
        timeout_s=5.0,
        classifier="read",
    ),
    CommandSpec(
        command="pins",
        purpose="Capture per-pin input, output latch, direction, and polarity summary.",
        expected=(r"=== Pin Summary ===",),
        timeout_s=8.0,
        classifier="read",
    ),
    CommandSpec(
        command="health",
        purpose="Capture driver health before stress.",
        expected=(r"=== Driver Health ===", r"State:\s+READY"),
        timeout_s=5.0,
        classifier="health",
        notes="Historical last-error fields may be nonzero; current READY state is the key gate.",
    ),
    CommandSpec(
        command="stress 10",
        purpose="Run bounded read-only input stress using the CLI async stress command.",
        expected=(r"=== Stress Results ===", r"fail=0"),
        timeout_s=20.0,
        classifier="stress",
        notes="This repeatedly reads inputs and clears PCA9555 interrupt state.",
    ),
    CommandSpec(
        command="health",
        purpose="Capture final driver health after stress.",
        expected=(r"=== Driver Health ===", r"State:\s+READY"),
        timeout_s=5.0,
        classifier="health",
    ),
)


OPTIONAL_COMMANDS: tuple[CommandSpec, ...] = (
    CommandSpec(
        command="selftest confirm",
        purpose="Run CLI API self-test that mutates output, direction, and polarity state.",
        expected=(r"Selftest result:", r"fail=0"),
        timeout_s=25.0,
        destructive=True,
        requires_opt_in="--include-output-tests",
        recovery_command="recover confirm",
        classifier="selftest",
        notes=(
            "This command changes PCA9555 latches, direction, and polarity before "
            "restoring them. Use only on a known-safe fixture."
        ),
    ),
    CommandSpec(
        command="stress 1000",
        purpose="Longer read-only stress soak.",
        expected=(r"=== Stress Results ===", r"fail=0"),
        timeout_s=240.0,
        requires_opt_in="--include-soak",
        classifier="stress",
        notes="Longer soak is optional and still clears input interrupt state.",
    ),
    CommandSpec(
        command="stress_mix 100 confirm",
        purpose="Mixed read/write/config/polarity/mask stress test.",
        expected=(r"=== stress_mix summary ===", r"fail=0"),
        timeout_s=180.0,
        destructive=True,
        requires_opt_in="--include-output-tests",
        recovery_command="recover confirm",
        classifier="stress_mix",
        notes="Mixed stress drives outputs and changes configuration; opt-in only.",
    ),
)


MANUAL_CHECKS: tuple[CommandSpec, ...] = (
    CommandSpec(
        command="operator: wiring-photo",
        purpose="Capture board, PCA9555 module, VCC, pull-ups, address straps, and load wiring.",
        operator_check=True,
        notes="Attach photos and a wiring table to the run record.",
    ),
    CommandSpec(
        command="operator: per-pin-inputs",
        purpose="Exercise all 16 input pins against the documented fixture levels.",
        operator_check=True,
        notes="Serial input values require physical stimulus notes to become evidence.",
    ),
    CommandSpec(
        command="operator: safe-output-observation",
        purpose="Observe physical outputs on current-limited loads during opt-in output tests.",
        operator_check=True,
        notes="Default runner does not toggle GPIO outputs.",
    ),
    CommandSpec(
        command="operator: int-clear",
        purpose="Observe INT assertion and clearing for each port.",
        operator_check=True,
        notes="Requires pull-up and logic analyzer or MCU capture.",
    ),
    CommandSpec(
        command="operator: errata-pointer-park",
        purpose="Decode I2C traffic after input reads and confirm command pointer parking.",
        operator_check=True,
        notes="Requires logic analyzer capture of the shared bus transaction sequence.",
    ),
    CommandSpec(
        command="operator: fault-recovery",
        purpose="Run unplug, wrong-address, brownout, and recovery checks only with safe handling.",
        operator_check=True,
        notes="Fault injection is never run by this script automatically.",
    ),
)


FAIL_CONTEXT_PATTERNS: tuple[str, ...] = (
    r"Status:\s+(?!OK\b)[A-Z_]+",
    r"\[FAIL\]",
    r"(?<!0\s)\bFAILED\b",
    r"\bfail=(?!0\b)\d+",
    r"\bfailed=(?!0\b)\d+",
    r"\[W\]\s+Unknown command",
    r"\[E\]\s+Usage:",
)


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text).replace("\r\n", "\n").replace("\r", "\n")


def git_output(args: list[str]) -> str:
    try:
        completed = subprocess.run(
            ["git", *args],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError:
        return "unavailable"
    if completed.returncode != 0:
        return (completed.stderr or completed.stdout).strip() or "unavailable"
    return completed.stdout.strip()


def create_log_dir(base: pathlib.Path) -> pathlib.Path:
    stamp = _dt.datetime.now().strftime("i2c_%Y%m%d_%H%M%S")
    for suffix in ("", *[f"_{i:02d}" for i in range(1, 100)]):
        candidate = base / f"{stamp}{suffix}"
        try:
            candidate.mkdir(parents=True, exist_ok=False)
            return candidate
        except FileExistsError:
            continue
    raise RuntimeError(f"could not create unique log directory under {base}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run PCA9555 serial CLI HIL checks and collect audit evidence."
    )
    parser.add_argument("--port", help="Serial port for the flashed CLI firmware.")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate.")
    parser.add_argument("--out", default="hil_logs", help="Base output directory.")
    parser.add_argument(
        "--serial-dtr",
        choices=("0", "1", "keep"),
        default="0",
        help="DTR line state after opening serial: 1=assert, 0=deassert, keep=leave pyserial default.",
    )
    parser.add_argument(
        "--serial-rts",
        choices=("0", "1", "keep"),
        default="0",
        help="RTS line state after opening serial: 1=assert, 0=deassert, keep=leave pyserial default.",
    )
    parser.add_argument(
        "--timeout",
        "--timeout-s",
        dest="timeout",
        type=float,
        default=None,
        help=(
            "Minimum command timeout in seconds. Known command-specific bounds "
            "are never shortened; custom command files use 5s when omitted."
        ),
    )
    parser.add_argument("--dry-run", action="store_true", help="Plan only; do not open serial.")
    parser.add_argument("--parser-self-test", action="store_true", help="Run parser/classifier self-test and exit.")
    parser.add_argument("--commands", help="Optional text or JSON command file.")
    parser.add_argument("--address", default=DEFAULT_ADDRESS, help="Expected I2C address.")
    parser.add_argument("--startup-timeout", type=float, default=30.0)
    parser.add_argument(
        "--idle-gap",
        "--idle-timeout-s",
        dest="idle_gap",
        type=float,
        default=0.5,
        help="Prompt-settle timeout after completion tokens, in seconds.",
    )
    parser.add_argument(
        "--allow-idle-completion",
        action="store_true",
        help="Allow idle serial output to complete a command before the prompt appears.",
    )
    parser.add_argument(
        "--boot-settle-s",
        type=float,
        default=0.0,
        help="Optional bounded delay after opening serial before prompt detection.",
    )
    parser.add_argument(
        "--reconnect-attempts",
        type=int,
        default=0,
        help="Optional bounded serial open retry count after the first attempt.",
    )
    parser.add_argument(
        "--reconnect-delay-s",
        type=float,
        default=1.0,
        help="Delay between bounded serial reconnect attempts.",
    )
    parser.add_argument(
        "--serial-reopen-interval-s",
        type=float,
        default=0.0,
        help=(
            "Close and reopen serial between aggregate commands after this interval. "
            "Zero disables. USB CDC boards may reset when the port is reopened."
        ),
    )
    parser.add_argument("--verbose", action="store_true", help="Print command progress.")
    parser.add_argument("--include-output-tests", action="store_true")
    parser.add_argument("--include-soak", action="store_true")
    parser.add_argument("--include-fault-tests", action="store_true")
    parser.add_argument("--benchmark-command", help="Optional command to repeat for sample-rate benchmarking.")
    parser.add_argument("--benchmark-count", type=int, default=0, help="Bounded benchmark sample count.")
    parser.add_argument("--benchmark-warmup", type=int, default=3, help="Bounded benchmark warmup count.")
    parser.add_argument(
        "--soak-duration-s",
        type=float,
        default=0.0,
        help="Optional bounded soak duration in seconds. Use 28800 for 8 hours.",
    )
    parser.add_argument(
        "--soak-max-commands",
        type=int,
        default=0,
        help="Optional bounded soak command limit. Zero means duration-bound only.",
    )
    parser.add_argument(
        "--soak-command-mix",
        default="read,outputs,config,polarity,health,probe",
        help="Comma-separated soak command mix. Unsafe commands remain opt-in gated.",
    )
    parser.add_argument("--soak-interval-s", type=float, default=0.5, help="Delay between soak commands.")
    parser.add_argument(
        "--soak-failure-limit",
        type=int,
        default=3,
        help="Stop soak after this many consecutive FAIL/TIMEOUT classifications.",
    )
    parser.add_argument("--report", help="Optional Markdown summary copy path.")
    parser.add_argument(
        "--prompt",
        default=PROMPT_TOKEN,
        help="CLI prompt token. Default matches examples/common/CliStyle.h.",
    )
    args = parser.parse_args(argv)
    validate_args(args, parser)
    return args


def validate_args(args: argparse.Namespace, parser: argparse.ArgumentParser) -> None:
    for field in (
        "baud",
        "timeout",
        "startup_timeout",
        "idle_gap",
        "boot_settle_s",
        "reconnect_delay_s",
        "serial_reopen_interval_s",
        "soak_interval_s",
    ):
        value = getattr(args, field)
        if value is not None and value < 0:
            parser.error(f"--{field.replace('_', '-')} must be non-negative")
    for field in (
        "reconnect_attempts",
        "benchmark_count",
        "benchmark_warmup",
        "soak_max_commands",
        "soak_failure_limit",
    ):
        if getattr(args, field) < 0:
            parser.error(f"--{field.replace('_', '-')} must be non-negative")
    if args.baud <= 0:
        parser.error("--baud must be positive")
    if args.timeout == 0:
        parser.error("--timeout-s must be positive")
    if args.startup_timeout == 0:
        parser.error("--startup-timeout must be positive")
    if args.idle_gap == 0:
        parser.error("--idle-timeout-s must be positive")
    if args.benchmark_command and args.benchmark_count == 0:
        parser.error("--benchmark-command requires --benchmark-count > 0")
    if args.soak_duration_s == 0 and args.soak_max_commands > 0:
        parser.error("--soak-max-commands requires --soak-duration-s > 0")
    if args.soak_duration_s > 0 and args.soak_failure_limit == 0:
        parser.error("--soak-failure-limit must be positive when soak is enabled")


def default_command_timeout(args: argparse.Namespace) -> float:
    return args.timeout if args.timeout is not None else 5.0


def load_command_file(path: pathlib.Path, default_timeout: float) -> list[CommandSpec]:
    text = path.read_text(encoding="utf-8")
    try:
        loaded = json.loads(text)
    except json.JSONDecodeError:
        commands = [
            line.strip()
            for line in text.splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        return [
            spec_for_command(command, default_timeout)
            for command in commands
        ]

    specs: list[CommandSpec] = []
    if not isinstance(loaded, list):
        raise ValueError("--commands JSON must contain a list")
    for entry in loaded:
        if isinstance(entry, str):
            specs.append(spec_for_command(entry, default_timeout))
            continue
        if not isinstance(entry, dict) or "command" not in entry:
            raise ValueError("each --commands JSON item must be a string or object with command")
        command = str(entry["command"])
        unsafe = command_is_unsafe(command)
        destructive = bool(entry.get("destructive", unsafe))
        requires_opt_in = (
            entry.get("requires_opt_in")
            or default_required_opt_in(command, unsafe=unsafe, destructive=destructive)
        )
        specs.append(
            CommandSpec(
                command=command,
                purpose=str(entry.get("purpose", "Custom operator-provided command.")),
                expected=tuple(str(item) for item in entry.get("expected", [])),
                timeout_s=float(entry.get("timeout_s", default_timeout)),
                completion_tokens=tuple(str(item) for item in entry.get("completion_tokens", [])),
                operator_check=bool(entry.get("operator_check", False)),
                destructive=destructive,
                requires_opt_in=requires_opt_in,
                recovery_command=entry.get("recovery_command"),
                notes=str(entry.get("notes", "")) or ("Custom command matched the unsafe CLI allowlist and requires opt-in." if unsafe else ""),
                classifier=str(entry.get("classifier", "custom")),
            )
        )
    return specs


def recovery_spec_for(spec: CommandSpec) -> CommandSpec | None:
    if not spec.recovery_command:
        return None
    dynamic = dynamic_cli_command_spec(spec.recovery_command, 5.0)
    if dynamic is not None:
        return dataclasses.replace(
            dynamic,
            purpose=f"Apply the configured recovery image after `{spec.command}`.",
            requires_opt_in=spec.requires_opt_in,
            notes=(
                "Automatic recovery command from HIL metadata. Applies the example "
                "image (latches high, normal polarity, all pins input); this is a "
                "fixture default, not a universal product-safe state."
            ),
        )
    return CommandSpec(
        command=spec.recovery_command,
        purpose=f"Apply the configured recovery image after `{spec.command}`.",
        expected=(r"Status:\s+OK",),
        timeout_s=5.0,
        destructive=True,
        requires_opt_in=spec.requires_opt_in,
        classifier="write",
        notes=(
            "Automatic recovery command from HIL metadata. Applies the example "
            "image (latches high, normal polarity, all pins input); this is a "
            "fixture default, not a universal product-safe state."
        ),
    )


def command_is_unsafe(command: str) -> bool:
    return UNSAFE_COMMAND_RE.search(command) is not None


def read_stress_cycle_count(command: str) -> int | None:
    match = READ_STRESS_COMMAND_RE.search(command)
    if match is None:
        return None
    if match.group(1) is None:
        return DEFAULT_SAFE_STRESS_CYCLES
    return int(match.group(1), 10)


def command_requires_soak_opt_in(command: str) -> bool:
    count = read_stress_cycle_count(command)
    return count is not None and count > DEFAULT_SAFE_STRESS_CYCLES


def default_required_opt_in(command: str, *, unsafe: bool, destructive: bool) -> str | None:
    if unsafe or destructive:
        return "--include-output-tests"
    if command_requires_soak_opt_in(command):
        return "--include-soak"
    return None


def custom_command_spec(command: str, default_timeout: float) -> CommandSpec:
    unsafe = command_is_unsafe(command)
    requires_opt_in = default_required_opt_in(command, unsafe=unsafe, destructive=unsafe)
    recovery_command = (
        "recover confirm"
        if unsafe and not re.fullmatch(r"\s*recover(?:\s+confirm)?\s*", command, re.IGNORECASE)
        else None
    )
    return CommandSpec(
        command=command,
        purpose="Custom operator-provided command.",
        timeout_s=default_timeout,
        classifier="custom",
        destructive=unsafe,
        requires_opt_in=requires_opt_in,
        recovery_command=recovery_command,
        notes=(
            "Custom command matched the unsafe CLI allowlist and requires opt-in."
            if unsafe
            else "Custom read-only stress exceeds the default safe cycle count and requires soak opt-in."
            if requires_opt_in == "--include-soak"
            else "Custom command classification is conservative."
        ),
    )


def dynamic_cli_command_spec(command: str, default_timeout: float) -> CommandSpec | None:
    text = command.strip()
    lowered = text.lower()
    destructive = command_is_unsafe(text)
    requires_opt_in = default_required_opt_in(text, unsafe=destructive, destructive=destructive)

    def make(
        *,
        purpose: str,
        expected: tuple[str, ...],
        classifier: str,
        timeout_s: float | None = None,
        completion_tokens: tuple[str, ...] = (),
    ) -> CommandSpec:
        return CommandSpec(
            command=text,
            purpose=purpose,
            expected=expected,
            timeout_s=timeout_s if timeout_s is not None else default_timeout,
            completion_tokens=completion_tokens,
            destructive=destructive,
            requires_opt_in=requires_opt_in,
            recovery_command=(
                "recover confirm"
                if destructive and not re.fullmatch(r"recover(?:\s+confirm)?", lowered)
                else None
            ),
            classifier=classifier,
            notes=(
                "Dynamic CLI command matched the unsafe output-control allowlist and requires opt-in."
                if requires_opt_in == "--include-output-tests"
                else "Dynamic CLI command."
            ),
        )

    if re.fullmatch(r"stress(?:\s+[1-9]\d*)?", lowered):
        return make(
            purpose="Run bounded read-only input and pointer-park stress.",
            expected=(r"=== Stress Results ===", r"fail=0"),
            classifier="stress",
        )
    if re.fullmatch(r"allhigh(?:\s+confirm)?", lowered):
        return make(
            purpose="Drive all PCA9555 pins high as outputs.",
            expected=(r"All 16 pins set to OUTPUT HIGH",),
            classifier="output_pattern",
        )
    if re.fullmatch(r"alllow(?:\s+confirm)?", lowered):
        return make(
            purpose="Drive all PCA9555 pins low as outputs.",
            expected=(r"All 16 pins set to OUTPUT LOW",),
            classifier="output_pattern",
        )
    if re.fullmatch(r"(?:pattern|pat)\s+0x[0-9a-f]{1,4}(?:\s+confirm)?", lowered):
        return make(
            purpose="Drive an exact 16-bit PCA9555 output pattern.",
            expected=(r"Pattern applied:\s+value=0x[0-9A-F]{4}",),
            classifier="output_pattern",
        )
    if re.fullmatch(r"sweep(?:\s+\d+)?(?:\s+confirm)?", lowered):
        return make(
            purpose="Run accumulating output ON/OFF sweep across all 16 pins.",
            expected=(r"=== Sweep Test", r"32 passed", r"0 failed"),
            timeout_s=max(default_timeout, 60.0),
            classifier="output_pattern",
        )
    if re.fullmatch(r"walk(?:\s+\d+)?(?:\s+confirm)?", lowered):
        return make(
            purpose="Run walking-1 output pattern across all 16 pins.",
            expected=(r"=== Walking-1 Test", r"16 passed", r"0 failed"),
            timeout_s=max(default_timeout, 60.0),
            classifier="output_pattern",
        )
    if re.fullmatch(r"(?:setbits|sb)\s+0x[0-9a-f]{1,4}(?:\s+confirm)?", lowered):
        return make(
            purpose="Set masked output latch bits high.",
            expected=(r"Output latch bits set HIGH:\s+mask=0x[0-9A-F]{4}",),
            classifier="mask_write",
        )
    if re.fullmatch(r"(?:clearbits|cb)\s+0x[0-9a-f]{1,4}(?:\s+confirm)?", lowered):
        return make(
            purpose="Clear masked output latch bits low.",
            expected=(r"Output latch bits cleared LOW:\s+mask=0x[0-9A-F]{4}",),
            classifier="mask_write",
        )
    if re.fullmatch(r"(?:togglebits|tb)\s+0x[0-9a-f]{1,4}(?:\s+confirm)?", lowered):
        return make(
            purpose="Toggle masked output latch bits.",
            expected=(r"Output latch bits toggled:\s+mask=0x[0-9A-F]{4}",),
            classifier="mask_write",
        )
    if re.fullmatch(r"dirin\s+0x[0-9a-f]{1,4}(?:\s+confirm)?", lowered):
        return make(
            purpose="Configure masked pins as inputs.",
            expected=(r"Pins configured as INPUT:\s+mask=0x[0-9A-F]{4}",),
            classifier="direction",
        )
    if re.fullmatch(r"dirout\s+0x[0-9a-f]{1,4}(?:\s+confirm)?", lowered):
        return make(
            purpose="Configure masked pins as outputs.",
            expected=(r"Pins configured as OUTPUT:\s+mask=0x[0-9A-F]{4}",),
            classifier="direction",
        )
    if re.fullmatch(r"invertset\s+0x[0-9a-f]{1,4}(?:\s+confirm)?", lowered):
        return make(
            purpose="Enable masked input polarity inversion.",
            expected=(r"Polarity inversion enabled:\s+mask=0x[0-9A-F]{4}",),
            classifier="polarity",
        )
    if re.fullmatch(r"invertclr\s+0x[0-9a-f]{1,4}(?:\s+confirm)?", lowered):
        return make(
            purpose="Disable masked input polarity inversion.",
            expected=(r"Polarity inversion disabled:\s+mask=0x[0-9A-F]{4}",),
            classifier="polarity",
        )
    if re.fullmatch(r"(?:write\s+pin|wpin)\s+\d{1,2}\s+[01](?:\s+confirm)?", lowered):
        return make(
            purpose="Write one output latch bit.",
            expected=(r"Output latch pin\s+\d+.*=\s+[01]",),
            classifier="pin_write",
        )
    if re.fullmatch(r"toggle\s+\d{1,2}(?:\s+confirm)?", lowered):
        return make(
            purpose="Toggle one output latch bit.",
            expected=(r"Output latch pin\s+\d+.*toggled",),
            classifier="pin_write",
        )
    if re.fullmatch(r"(?:dir\s+pin|dir)\s+\d{1,2}\s+(?:in|out)(?:\s+confirm)?", lowered):
        return make(
            purpose="Set one pin direction.",
            expected=(r"Pin\s+\d+.*set to\s+(?:INPUT|OUTPUT)",),
            classifier="direction",
        )
    if re.fullmatch(r"(?:write\s+port|wport)\s+[01]\s+0x[0-9a-f]{1,2}(?:\s+confirm)?", lowered):
        return make(
            purpose="Write one output port latch register.",
            expected=(r"Port\s+[01]\s+output latch set to 0x[0-9A-F]{2}",),
            classifier="port_write",
        )
    if re.fullmatch(r"(?:dir\s+port|dport)\s+[01]\s+0x[0-9a-f]{1,2}(?:\s+confirm)?", lowered):
        return make(
            purpose="Set one port direction register.",
            expected=(r"Port\s+[01]\s+config set to 0x[0-9A-F]{2}",),
            classifier="direction",
        )
    if re.fullmatch(r"(?:polarity\s+pin|pol)\s+\d{1,2}\s+[01](?:\s+confirm)?", lowered):
        return make(
            purpose="Set one pin input polarity inversion bit.",
            expected=(r"Pin\s+\d+.*polarity set to\s+(?:INVERTED|NORMAL)",),
            classifier="polarity",
        )
    if re.fullmatch(r"(?:polarity\s+port|wpol)\s+[01]\s+0x[0-9a-f]{1,2}(?:\s+confirm)?", lowered):
        return make(
            purpose="Set one port polarity inversion register.",
            expected=(r"Port\s+[01]\s+polarity set to 0x[0-9A-F]{2}",),
            classifier="polarity",
        )
    if re.fullmatch(r"(?:read\s+reg|rreg)\s+[0-7]", lowered):
        return make(
            purpose="Read one PCA9555 register.",
            expected=(r"Reg\s+0x0?[0-7]\s+=\s+0x[0-9A-F]{2}",),
            classifier="register_read",
        )
    if re.fullmatch(r"(?:read\s+regs|rregs)\s+[0-7]\s+[12]", lowered):
        return make(
            purpose="Read one or two PCA9555 registers with pair wrapping.",
            expected=(r"Regs\s+0x0?[0-7]=0x[0-9A-F]{2}",),
            classifier="register_read",
        )
    if re.fullmatch(r"(?:write\s+reg|wreg)\s+[2-7]\s+0x[0-9a-f]{1,2}(?:\s+confirm)?", lowered):
        return make(
            purpose="Write one writable PCA9555 register.",
            expected=(r"Reg\s+0x0?[2-7]\s+set to 0x[0-9A-F]{2}",),
            classifier="register_write",
        )
    if re.fullmatch(r"(?:write\s+regs|wregs)\s+[2-7]\s+0x[0-9a-f]{1,2}(?:\s+0x[0-9a-f]{1,2})?(?:\s+confirm)?", lowered):
        return make(
            purpose="Write one or two writable PCA9555 registers with pair wrapping.",
            expected=(r"Regs?\s+0x0?[2-7].*0x[0-9A-F]{2}",),
            classifier="register_write",
        )
    if re.fullmatch(r"recover(?:\s+confirm)?", lowered):
        return make(
            purpose="Apply the explicit example recovery image.",
            expected=(r"Attempting recovery", r"Status:\s+OK"),
            timeout_s=max(default_timeout, 15.0),
            classifier="recovery",
        )
    return None


def address_table_token(address: str) -> str:
    text = address.strip().lower()
    if text.startswith("0x"):
        try:
            return f"{int(text, 16):02X}"
        except ValueError:
            return text.upper()
    try:
        return f"{int(text, 0):02X}"
    except ValueError:
        return text.upper()


def opt_in_enabled(spec: CommandSpec, args: argparse.Namespace) -> bool:
    if spec.requires_opt_in is None:
        return True
    if spec.requires_opt_in == "--include-output-tests":
        return bool(args.include_output_tests)
    if spec.requires_opt_in == "--include-soak":
        return bool(args.include_soak)
    if spec.requires_opt_in == "--include-fault-tests":
        return bool(args.include_fault_tests)
    return False


def build_command_sequence(args: argparse.Namespace) -> list[CommandSpec]:
    custom_timeout = default_command_timeout(args)
    if args.commands:
        custom_specs: list[CommandSpec] = []
        for spec in load_command_file(pathlib.Path(args.commands), custom_timeout):
            spec = apply_timeout_override(spec, args)
            custom_specs.append(spec)
            recovery = recovery_spec_for(spec)
            if recovery is not None:
                recovery = apply_timeout_override(recovery, args)
                custom_specs.append(recovery)
        return custom_specs
    address_token = address_table_token(args.address)
    default_specs: list[CommandSpec] = []
    for spec in DEFAULT_SAFE_COMMANDS:
        if spec.command == "scan":
            spec = dataclasses.replace(
                spec,
                expected=(r"Scan complete", rf"(?m)^[0-7][0-9A-F]:\s+.*\b{re.escape(address_token)}\b"),
            )
        spec = apply_timeout_override(spec, args)
        default_specs.append(spec)
    all_specs: list[CommandSpec] = [*default_specs]
    for spec in OPTIONAL_COMMANDS:
        spec = apply_timeout_override(spec, args)
        all_specs.append(spec)
        recovery = recovery_spec_for(spec)
        if recovery is not None:
            recovery = apply_timeout_override(recovery, args)
            all_specs.append(recovery)
    return all_specs


def spec_for_command(command: str, default_timeout: float) -> CommandSpec:
    for spec in (*DEFAULT_SAFE_COMMANDS, *OPTIONAL_COMMANDS):
        if spec.command == command:
            return spec
    dynamic = dynamic_cli_command_spec(command, default_timeout)
    if dynamic is not None:
        return dynamic
    return custom_command_spec(command, default_timeout)


def parse_command_mix(text: str, default_timeout: float) -> list[CommandSpec]:
    commands = [item.strip() for item in text.split(",") if item.strip()]
    return [spec_for_command(command, default_timeout) for command in commands]


def apply_timeout_override(spec: CommandSpec, args: argparse.Namespace) -> CommandSpec:
    if args.timeout is None:
        return spec
    return dataclasses.replace(spec, timeout_s=max(spec.timeout_s, args.timeout))


def extract_evidence(text: str, patterns: Iterable[str]) -> list[str]:
    evidence: list[str] = []
    lines = text.splitlines()
    for pattern in patterns:
        regex = re.compile(pattern, re.IGNORECASE)
        for line in lines:
            if regex.search(line):
                evidence.append(line.strip())
                break
    return evidence[:6]


def has_failure_context(text: str) -> bool:
    for pattern in FAIL_CONTEXT_PATTERNS:
        if re.search(pattern, text, flags=re.IGNORECASE):
            return True
    return False


def classify(spec: CommandSpec, normalized: str, completion_reason: str) -> tuple[str, list[str]]:
    if completion_reason == "timeout":
        return "TIMEOUT", extract_evidence(normalized, spec.expected or FAIL_CONTEXT_PATTERNS)
    if spec.operator_check:
        return "OPERATOR_CHECK_REQUIRED", []
    if has_failure_context(normalized):
        return "FAIL", extract_evidence(normalized, FAIL_CONTEXT_PATTERNS)

    expected_ok = True
    for pattern in spec.expected:
        if not re.search(pattern, normalized, flags=re.IGNORECASE):
            expected_ok = False
            break
    if expected_ok and spec.expected:
        return "PASS", extract_evidence(normalized, spec.expected)
    if normalized.strip():
        return "SERIAL_OK_OR_REVIEW", normalized.strip().splitlines()[-4:]
    return "REVIEW_REQUIRED", []


def import_serial_module():
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        print(f"pyserial is required for real serial runs. Install with: {PYSERIAL_HINT}", file=sys.stderr)
        return None
    return serial


def run_parser_self_test() -> int:
    args = parse_args(["--dry-run", "--address", "0x24"])
    specs = build_command_sequence(args)
    commands = [spec.command for spec in DEFAULT_SAFE_COMMANDS]
    required = {"version", "scan", "probe", "settings", "health"}
    missing = sorted(required.difference(commands))
    if missing:
        print(f"parser self-test missing default commands: {missing}", file=sys.stderr)
        return 1
    if any(spec.destructive or spec.requires_opt_in for spec in DEFAULT_SAFE_COMMANDS):
        print("parser self-test found unsafe default command", file=sys.stderr)
        return 1
    scan = next(spec for spec in specs if spec.command == "scan")
    if not any(re.search(pattern, "24: 20 24 27", flags=re.IGNORECASE) for pattern in scan.expected):
        print("parser self-test scan regex does not honor configured address", file=sys.stderr)
        return 1
    probe = CommandSpec(command="probe", purpose="self-test", expected=(r"Status:\s+OK",))
    ok_result, _ = classify(probe, "Status: OK\n", "prompt")
    fail_result, _ = classify(probe, "Status: I2C_TIMEOUT\n", "prompt")
    timeout_result, _ = classify(probe, "Status: OK\n", "timeout")
    if (ok_result, fail_result, timeout_result) != ("PASS", "FAIL", "TIMEOUT"):
        print("parser self-test classifier results are incorrect", file=sys.stderr)
        return 1
    if custom_command_spec("allhigh", 5.0).requires_opt_in != "--include-output-tests":
        print("parser self-test unsafe command is not opt-in gated", file=sys.stderr)
        return 1
    print("run_i2c_hil parser self-test: PASS")
    return 0


def serial_line_state(value: str) -> bool | None:
    if value == "keep":
        return None
    return value == "1"


def apply_serial_line_state(ser, args: argparse.Namespace) -> None:
    dtr = serial_line_state(args.serial_dtr)
    rts = serial_line_state(args.serial_rts)
    if dtr is not None:
        ser.dtr = dtr
    if rts is not None:
        ser.rts = rts


def decoded_ends_with_prompt(decoded: str, prompt: str) -> bool:
    if not decoded.endswith(prompt):
        return False
    prompt_start = len(decoded) - len(prompt)
    return prompt_start == 0 or decoded[prompt_start - 1] in "\r\n"


def open_serial_with_retries(serial_mod, args: argparse.Namespace):
    attempts = args.reconnect_attempts + 1
    last_exc: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            ser = serial_mod.Serial(args.port, args.baud, timeout=0.05)
            apply_serial_line_state(ser, args)
            return ser
        except Exception as exc:  # pragma: no cover - hardware path
            last_exc = exc
            if args.verbose:
                print(f"serial open attempt {attempt}/{attempts} failed: {exc}", file=sys.stderr)
            if attempt < attempts:
                time.sleep(args.reconnect_delay_s)
    raise RuntimeError(f"failed to open serial port {args.port}: {last_exc}")


def read_until(
    ser,
    *,
    prompt: str,
    timeout_s: float,
    idle_gap_s: float,
    allow_idle_completion: bool = False,
    completion_tokens: tuple[str, ...] = (),
    expected_patterns: tuple[str, ...] = (),
) -> tuple[str, str]:
    deadline = time.monotonic() + timeout_s
    chunks: list[bytes] = []
    last_rx = time.monotonic()
    saw_output = False
    evidence_complete = False

    while time.monotonic() < deadline:
        waiting = getattr(ser, "in_waiting", 0)
        read_size = waiting if waiting and waiting > 0 else 1
        data = ser.read(read_size)
        if data:
            chunks.append(data)
            last_rx = time.monotonic()
            saw_output = True
            decoded = b"".join(chunks).decode("utf-8", errors="replace")
            normalized = strip_ansi(decoded)
            patterns_complete = bool(expected_patterns) and all(
                re.search(pattern, normalized, flags=re.IGNORECASE)
                for pattern in expected_patterns
            )
            tokens_complete = any(
                token and token in decoded for token in completion_tokens
            )
            evidence_complete = patterns_complete or tokens_complete
            failure_seen = has_failure_context(normalized)
            framing_required = bool(expected_patterns or completion_tokens)
            if decoded_ends_with_prompt(decoded, prompt) and (
                evidence_complete or failure_seen or not framing_required
            ):
                return decoded, "prompt"
        elif saw_output and (time.monotonic() - last_rx) >= idle_gap_s and (
            evidence_complete or allow_idle_completion
        ):
            reason = "expected_evidence" if evidence_complete else "idle"
            return b"".join(chunks).decode("utf-8", errors="replace"), reason
        else:
            time.sleep(0.02)

    return b"".join(chunks).decode("utf-8", errors="replace"), "timeout"


def run_serial_command(
    ser,
    spec: CommandSpec,
    *,
    prompt: str,
    idle_gap_s: float,
    allow_idle_completion: bool,
) -> tuple[CommandResult, str]:
    start = time.monotonic()
    ser.write((spec.command + "\n").encode("utf-8"))
    ser.flush()
    raw, reason = read_until(
        ser,
        prompt=prompt,
        timeout_s=spec.timeout_s,
        idle_gap_s=idle_gap_s,
        allow_idle_completion=allow_idle_completion,
        completion_tokens=spec.completion_tokens,
        expected_patterns=spec.expected,
    )
    elapsed = time.monotonic() - start
    normalized = strip_ansi(raw)
    if decoded_ends_with_prompt(normalized, prompt):
        normalized = normalized[: -len(prompt)]
    result, evidence = classify(spec, normalized, reason)
    operator_result = "OPERATOR_CHECK_REQUIRED" if spec.operator_check else "N/A"
    command_result = CommandResult(
            command=spec.command,
            purpose=spec.purpose,
            classifier=spec.classifier,
            serial_result=result,
            operator_result=operator_result,
            completion_reason=reason,
            elapsed_s=elapsed,
            notes=spec.notes,
            evidence=evidence,
        )
    return command_result, raw


def command_result_is_failure(result: CommandResult) -> bool:
    return result.serial_result in {"FAIL", "TIMEOUT"}


def command_result_is_serial_anomaly(result: CommandResult) -> bool:
    return result.serial_result in {
        "FAIL",
        "TIMEOUT",
        "SERIAL_OK_OR_REVIEW",
        "REVIEW_REQUIRED",
        "SKIPPED_STARTUP_TIMEOUT",
    }


def run_aggregate_commands(
    ser,
    specs: list[CommandSpec],
    *,
    label: str,
    max_commands: int,
    deadline_s: float | None,
    prompt: str,
    idle_gap_s: float,
    allow_idle_completion: bool,
    interval_s: float,
    failure_limit: int,
    verbose: bool,
    serial_mod=None,
    args: argparse.Namespace | None = None,
) -> tuple[AggregateStats, list[CommandResult], object]:
    started_wall = _dt.datetime.now()
    started_mono = time.monotonic()
    deadline = started_mono + deadline_s if deadline_s is not None else None
    results: list[CommandResult] = []
    latencies: list[float] = []
    command_counts: dict[str, int] = {}
    result_counts: dict[str, int] = {}
    consecutive_failures = 0
    stop_reason = "limit_reached"
    serial_reopens = 0
    first_anomaly: CommandResult | None = None
    last_reopen_mono = started_mono
    reopen_interval_s = args.serial_reopen_interval_s if args is not None else 0.0

    index = 0
    while True:
        if max_commands > 0 and len(results) >= max_commands:
            stop_reason = "command_limit"
            break
        now = time.monotonic()
        if deadline is not None and now >= deadline:
            stop_reason = "duration_limit"
            break
        if not specs:
            stop_reason = "empty_command_mix"
            break

        if (
            reopen_interval_s > 0
            and serial_mod is not None
            and args is not None
            and (now - last_reopen_mono) >= reopen_interval_s
        ):
            try:
                ser.close()
            except Exception:
                pass
            try:
                ser = open_serial_with_retries(serial_mod, args)
            except RuntimeError as exc:  # pragma: no cover - hardware path
                result = CommandResult(
                    command=f"{label}:serial-reopen",
                    purpose="Reopen serial session between aggregate commands.",
                    classifier="serial",
                    serial_result="TIMEOUT",
                    operator_result="N/A",
                    completion_reason="serial_reopen",
                    elapsed_s=0.0,
                    notes=str(exc),
                    evidence=[str(exc)],
                )
                results.append(result)
                result_counts[result.serial_result] = result_counts.get(result.serial_result, 0) + 1
                stop_reason = "serial_reopen_failed"
                break
            serial_reopens += 1
            last_reopen_mono = time.monotonic()
            if args.boot_settle_s > 0:
                time.sleep(args.boot_settle_s)

        spec = specs[index % len(specs)]
        index += 1
        if hasattr(ser, "reset_input_buffer"):
            ser.reset_input_buffer()
        result, _ = run_serial_command(
            ser,
            spec,
            prompt=prompt,
            idle_gap_s=idle_gap_s,
            allow_idle_completion=allow_idle_completion,
        )
        results.append(result)
        latencies.append(result.elapsed_s)
        command_counts[result.command] = command_counts.get(result.command, 0) + 1
        result_counts[result.serial_result] = result_counts.get(result.serial_result, 0) + 1
        if verbose:
            print(f"{label}: {result.command} -> {result.serial_result} ({result.elapsed_s:.3f}s)")

        step_has_anomaly = command_result_is_serial_anomaly(result)
        if step_has_anomaly and first_anomaly is None:
            first_anomaly = result
        recovery = recovery_spec_for(spec)
        if recovery is not None:
            if args is not None:
                recovery = apply_timeout_override(recovery, args)
            if hasattr(ser, "reset_input_buffer"):
                ser.reset_input_buffer()
            recovery_result, _ = run_serial_command(
                ser,
                recovery,
                prompt=prompt,
                idle_gap_s=idle_gap_s,
                allow_idle_completion=allow_idle_completion,
            )
            results.append(recovery_result)
            latencies.append(recovery_result.elapsed_s)
            command_counts[recovery_result.command] = (
                command_counts.get(recovery_result.command, 0) + 1
            )
            result_counts[recovery_result.serial_result] = (
                result_counts.get(recovery_result.serial_result, 0) + 1
            )
            step_has_anomaly = step_has_anomaly or command_result_is_serial_anomaly(
                recovery_result
            )
            if command_result_is_serial_anomaly(recovery_result) and first_anomaly is None:
                first_anomaly = recovery_result
            if verbose:
                print(
                    f"{label}: {recovery_result.command} -> "
                    f"{recovery_result.serial_result} ({recovery_result.elapsed_s:.3f}s)"
                )

        if step_has_anomaly:
            consecutive_failures += 1
            if consecutive_failures >= failure_limit:
                stop_reason = "failure_limit"
                break
        else:
            consecutive_failures = 0

        if interval_s > 0:
            remaining = None if deadline is None else max(0.0, deadline - time.monotonic())
            sleep_s = interval_s if remaining is None else min(interval_s, remaining)
            if sleep_s > 0:
                time.sleep(sleep_s)

    ended_wall = _dt.datetime.now()
    elapsed = time.monotonic() - started_mono
    failures = sum(1 for result in results if command_result_is_failure(result))
    stats = AggregateStats(
        label=label,
        command_counts=command_counts,
        result_counts=result_counts,
        started_at=started_wall.isoformat(timespec="seconds"),
        ended_at=ended_wall.isoformat(timespec="seconds"),
        elapsed_s=elapsed,
        completed=len(results),
        failures=failures,
        min_latency_s=min(latencies) if latencies else None,
        mean_latency_s=(sum(latencies) / len(latencies)) if latencies else None,
        max_latency_s=max(latencies) if latencies else None,
        effective_hz=(len(results) / elapsed) if elapsed > 0 else 0.0,
        stop_reason=stop_reason,
        serial_reopens=serial_reopens,
        first_anomaly_command=(first_anomaly.command if first_anomaly else None),
        first_anomaly_result=(first_anomaly.serial_result if first_anomaly else None),
        first_anomaly_reason=(first_anomaly.completion_reason if first_anomaly else None),
        first_anomaly_evidence=(first_anomaly.evidence[:4] if first_anomaly else []),
    )
    return stats, results, ser


def aggregate_result(stats: AggregateStats) -> CommandResult:
    nonpass_counts = {
        key: value
        for key, value in stats.result_counts.items()
        if key != "PASS" and value > 0
    }
    status = "FAIL" if stats.failures > 0 else "PASS"
    if status == "PASS" and nonpass_counts:
        if nonpass_counts.get("TIMEOUT", 0) > 0 or nonpass_counts.get("FAIL", 0) > 0:
            status = "FAIL"
        elif nonpass_counts.get("SERIAL_OK_OR_REVIEW", 0) > 0:
            status = "SERIAL_OK_OR_REVIEW"
        else:
            status = "REVIEW_REQUIRED"
    if stats.completed == 0:
        status = "REVIEW_REQUIRED"
    evidence = [
        f"completed={stats.completed}",
        f"failures={stats.failures}",
    ]
    if stats.first_anomaly_command is not None:
        evidence.append(
            "first_anomaly="
            f"{stats.first_anomaly_command}/{stats.first_anomaly_result}/"
            f"{stats.first_anomaly_reason}"
        )
        evidence.extend(stats.first_anomaly_evidence[:2])
    evidence.extend(
        [
            f"result_counts={json.dumps(stats.result_counts, sort_keys=True)}",
            f"effective_hz={stats.effective_hz:.3f}",
            f"stop_reason={stats.stop_reason}",
        ]
    )
    return CommandResult(
        command=f"{stats.label}:aggregate",
        purpose=f"Aggregate statistics for {stats.label}.",
        classifier=stats.label,
        serial_result=status,
        operator_result="N/A",
        completion_reason=stats.stop_reason,
        elapsed_s=stats.elapsed_s,
        notes="Generated by bounded repeated-command runner.",
        evidence=evidence,
    )


def skipped_result(spec: CommandSpec, result: str, reason: str) -> CommandResult:
    operator_result = "OPERATOR_CHECK_REQUIRED" if spec.operator_check else "N/A"
    return CommandResult(
        command=spec.command,
        purpose=spec.purpose,
        classifier=spec.classifier,
        serial_result=result,
        operator_result=operator_result,
        completion_reason=reason,
        elapsed_s=0.0,
        notes=spec.notes,
        evidence=[],
    )


def final_verdict(results: list[CommandResult], dry_run: bool) -> str:
    if dry_run:
        return "INCOMPLETE"
    serial_results = [result.serial_result for result in results]
    if any(value in {"FAIL", "TIMEOUT"} for value in serial_results):
        return "FAIL"
    if any(
        value
        in {
            "SERIAL_OK_OR_REVIEW",
            "REVIEW_REQUIRED",
            "OPERATOR_CHECK_REQUIRED",
            "SKIPPED_UNSAFE",
            "NOT_IMPLEMENTED",
        }
        for value in serial_results
    ):
        return "OPERATOR_REVIEW_REQUIRED"
    if MANUAL_CHECKS:
        return "OPERATOR_REVIEW_REQUIRED"
    return "PASS"


def exit_code_for_verdict(verdict: str) -> int:
    return 1 if verdict == "FAIL" else 0


def exit_code_for_results(results: list[CommandResult], dry_run: bool) -> int:
    if dry_run:
        return 0
    if any(
        result.serial_result in {"FAIL", "TIMEOUT", "SERIAL_OK_OR_REVIEW", "REVIEW_REQUIRED"}
        for result in results
    ):
        return 1
    return 0


def write_summary_files(
    log_dir: pathlib.Path,
    args: argparse.Namespace,
    results: list[CommandResult],
    skipped: list[CommandSpec],
    aggregate_stats: list[AggregateStats],
) -> None:
    branch = git_output(["branch", "--show-current"])
    commit = git_output(["rev-parse", "HEAD"])
    status_short = git_output(["status", "--short"])
    verdict = final_verdict(results, args.dry_run)
    now = _dt.datetime.now().isoformat(timespec="seconds")
    summary_json = log_dir / "summary.json"
    summary_md = log_dir / "summary.md"
    artifacts = {
        "summary_json": str(summary_json),
        "summary_md": str(summary_md),
    }
    if args.report:
        artifacts["report"] = str(pathlib.Path(args.report))
    data = {
        "timestamp": now,
        "repo": str(pathlib.Path.cwd()),
        "branch": branch,
        "commit": commit,
        "worktree_status": status_short if status_short else "clean",
        "serial_port": args.port,
        "baud": args.baud,
        "serial_dtr": args.serial_dtr,
        "serial_rts": args.serial_rts,
        "i2c_address": args.address,
        "timeout_minimum_s": args.timeout,
        "startup_timeout_s": args.startup_timeout,
        "idle_timeout_s": args.idle_gap,
        "allow_idle_completion": args.allow_idle_completion,
        "boot_settle_s": args.boot_settle_s,
        "reconnect_attempts": args.reconnect_attempts,
        "serial_reopen_interval_s": args.serial_reopen_interval_s,
        "dry_run": args.dry_run,
        "final_verdict": verdict,
        "pyserial_install_hint": PYSERIAL_HINT,
        "artifacts": artifacts,
        "commands": [
            {
                "command": result.command,
                "classifier": result.classifier,
                "serial_result": result.serial_result,
                "operator_result": result.operator_result,
                "completion_reason": result.completion_reason,
                "elapsed_s": round(result.elapsed_s, 3),
                "evidence": result.evidence[:6],
            }
            for result in results
        ],
        "aggregate_stats": [dataclasses.asdict(stats) for stats in aggregate_stats],
        "skipped_opt_in_commands": [spec.command for spec in skipped],
        "manual_checks": [spec.command for spec in MANUAL_CHECKS],
        "identity_note": (
            "I2C ACK and scan results prove only address response. PCA9555 has no "
            "documented chip ID register, so this runner does not claim chip identity."
        ),
    }
    summary_json.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    result_counts: dict[str, int] = {}
    for result in results:
        result_counts[result.serial_result] = result_counts.get(result.serial_result, 0) + 1
    counts_text = ", ".join(f"{key}={value}" for key, value in sorted(result_counts.items())) or "none"
    findings = [
        result
        for result in results
        if result.serial_result in {"FAIL", "TIMEOUT", "SERIAL_OK_OR_REVIEW", "REVIEW_REQUIRED"}
    ]

    lines = [
        "# PCA9555 I2C HIL Summary",
        "",
        f"- Date/time: {now}",
        f"- Revision: `{branch}` at `{commit}` ({status_short if status_short else 'clean'})",
        f"- Target: `{args.port or 'N/A'}` at `{args.baud}` baud, PCA9555 `{args.address}`",
        f"- Serial lines: DTR `{args.serial_dtr}`, RTS `{args.serial_rts}`; reopen interval `{args.serial_reopen_interval_s}s`",
        f"- Timing: startup `{args.startup_timeout}s`, idle `{args.idle_gap}s`, command minimum `{args.timeout}`",
        f"- Dry run: `{args.dry_run}`",
        f"- Final verdict: `{verdict}`",
        "",
        "## Results",
        "",
        f"- Command results: {counts_text}",
        f"- Detailed machine results: `{summary_json}`",
        "- Raw CLI output is not retained; only bounded classifier evidence is kept for findings.",
    ]
    if findings:
        lines.extend(["", "### Findings", ""])
        for result in findings:
            evidence = "; ".join(item.replace("|", "/") for item in result.evidence[:3])
            lines.append(
                f"- `{result.command}`: `{result.serial_result}` via "
                f"`{result.completion_reason}` ({result.elapsed_s:.2f}s)"
                + (f" — {evidence}" if evidence else "")
            )
    if aggregate_stats:
        lines.extend(["", "## Aggregate Timing", ""])
        lines.extend(
            [
                "| Label | Completed | Failures | Serial Reopens | Min | Mean | Max | Effective Hz | Stop Reason |",
                "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
            ]
        )
        for stats in aggregate_stats:
            min_s = "" if stats.min_latency_s is None else f"{stats.min_latency_s:.3f}s"
            mean_s = "" if stats.mean_latency_s is None else f"{stats.mean_latency_s:.3f}s"
            max_s = "" if stats.max_latency_s is None else f"{stats.max_latency_s:.3f}s"
            lines.append(
                f"| `{stats.label}` | {stats.completed} | {stats.failures} | {stats.serial_reopens} | "
                f"{min_s} | {mean_s} | {max_s} | {stats.effective_hz:.3f} | `{stats.stop_reason}` |"
            )
    lines.extend(
        [
            "",
            "## Open Operator Checks",
            "",
            ", ".join(f"`{spec.command}`" for spec in MANUAL_CHECKS) + ".",
        ]
    )
    if skipped:
        lines.extend(
            [
                "",
                "Skipped opt-in commands: " + ", ".join(f"`{spec.command}`" for spec in skipped) + ".",
            ]
        )
    lines.extend(
        [
            "",
            "## Claim Guard",
            "",
            "A dry run is not physical HIL. A scan or probe proves only I2C ACK at the address, not PCA9555 identity.",
        ]
    )
    summary_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if args.report:
        report_path = pathlib.Path(args.report)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(summary_md, report_path)


def run(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.parser_self_test:
        return run_parser_self_test()

    log_dir = create_log_dir(pathlib.Path(args.out))

    specs = build_command_sequence(args)
    skipped: list[CommandSpec] = []
    results: list[CommandResult] = []
    aggregate_stats: list[AggregateStats] = []

    if args.dry_run:
        for spec in specs:
            if spec.operator_check:
                results.append(skipped_result(spec, "OPERATOR_CHECK_REQUIRED", "manual"))
            elif (spec.destructive or spec.requires_opt_in) and not opt_in_enabled(spec, args):
                skipped.append(spec)
                results.append(skipped_result(spec, "SKIPPED_UNSAFE", spec.requires_opt_in or "manual"))
            else:
                results.append(skipped_result(spec, "SKIPPED_DRY_RUN", "dry_run"))
    else:
        if not args.port:
            print("--port is required unless --dry-run is used", file=sys.stderr)
            return 2
        serial_mod = import_serial_module()
        if serial_mod is None:
            return 2
        try:
            ser = open_serial_with_retries(serial_mod, args)
        except RuntimeError as exc:  # pragma: no cover - hardware path
            print(str(exc), file=sys.stderr)
            return 2
        try:  # pragma: no cover - hardware path
            if args.boot_settle_s > 0:
                time.sleep(args.boot_settle_s)
            startup_start = time.monotonic()
            _, startup_reason = read_until(
                ser,
                prompt=args.prompt,
                timeout_s=args.startup_timeout,
                idle_gap_s=args.idle_gap,
                allow_idle_completion=args.allow_idle_completion,
            )
            startup_elapsed = time.monotonic() - startup_start
            if startup_reason == "timeout":
                results.append(
                    CommandResult(
                        command="startup",
                        purpose="Wait for CLI startup prompt.",
                        classifier="startup",
                        serial_result="TIMEOUT",
                        operator_result="N/A",
                        completion_reason="timeout",
                        elapsed_s=startup_elapsed,
                        notes="No CLI prompt observed before startup timeout.",
                        evidence=[],
                    )
                )
                for spec in specs:
                    if spec.operator_check:
                        results.append(skipped_result(spec, "OPERATOR_CHECK_REQUIRED", "manual"))
                    elif (spec.destructive or spec.requires_opt_in) and not opt_in_enabled(spec, args):
                        skipped.append(spec)
                        results.append(skipped_result(spec, "SKIPPED_UNSAFE", spec.requires_opt_in or "manual"))
                    else:
                        results.append(skipped_result(spec, "SKIPPED_STARTUP_TIMEOUT", "startup_timeout"))
            else:
                for spec in specs:
                    if spec.operator_check:
                        results.append(skipped_result(spec, "OPERATOR_CHECK_REQUIRED", "manual"))
                        continue
                    if (spec.destructive or spec.requires_opt_in) and not opt_in_enabled(spec, args):
                        skipped.append(spec)
                        results.append(skipped_result(spec, "SKIPPED_UNSAFE", spec.requires_opt_in or "manual"))
                        continue
                    if hasattr(ser, "reset_input_buffer"):
                        ser.reset_input_buffer()
                    result, _ = run_serial_command(
                        ser,
                        spec,
                        prompt=args.prompt,
                        idle_gap_s=args.idle_gap,
                        allow_idle_completion=args.allow_idle_completion,
                    )
                    results.append(result)

                serial_setup_clean = not any(command_result_is_serial_anomaly(result) for result in results)

                if serial_setup_clean and args.benchmark_command:
                    bench_spec = apply_timeout_override(
                        spec_for_command(args.benchmark_command, default_command_timeout(args)),
                        args,
                    )
                    if (bench_spec.destructive or bench_spec.requires_opt_in) and not opt_in_enabled(bench_spec, args):
                        skipped.append(bench_spec)
                        results.append(skipped_result(bench_spec, "SKIPPED_UNSAFE", bench_spec.requires_opt_in or "manual"))
                    else:
                        for warmup_index in range(args.benchmark_warmup):
                            if hasattr(ser, "reset_input_buffer"):
                                ser.reset_input_buffer()
                            warmup_result, _ = run_serial_command(
                                ser,
                                bench_spec,
                                prompt=args.prompt,
                                idle_gap_s=args.idle_gap,
                                allow_idle_completion=args.allow_idle_completion,
                            )
                            if args.verbose:
                                print(
                                    f"benchmark warmup {warmup_index + 1}: "
                                    f"{warmup_result.serial_result} ({warmup_result.elapsed_s:.3f}s)"
                                )
                            if command_result_is_failure(warmup_result):
                                results.append(warmup_result)
                                break
                        else:
                            stats, _, ser = run_aggregate_commands(
                                ser,
                                [bench_spec],
                                label="benchmark",
                                max_commands=args.benchmark_count,
                                deadline_s=None,
                                prompt=args.prompt,
                                idle_gap_s=args.idle_gap,
                                allow_idle_completion=args.allow_idle_completion,
                                interval_s=0.0,
                                failure_limit=1,
                                verbose=args.verbose,
                                serial_mod=serial_mod,
                                args=args,
                            )
                            aggregate_stats.append(stats)
                            results.append(aggregate_result(stats))

                if serial_setup_clean and args.soak_duration_s > 0:
                    soak_specs = [
                        apply_timeout_override(spec, args)
                        for spec in parse_command_mix(args.soak_command_mix, default_command_timeout(args))
                    ]
                    runnable: list[CommandSpec] = []
                    for spec in soak_specs:
                        if (spec.destructive or spec.requires_opt_in) and not opt_in_enabled(spec, args):
                            skipped.append(spec)
                            results.append(skipped_result(spec, "SKIPPED_UNSAFE", spec.requires_opt_in or "manual"))
                        else:
                            runnable.append(spec)
                    stats, _, ser = run_aggregate_commands(
                        ser,
                        runnable,
                        label="soak",
                        max_commands=args.soak_max_commands,
                        deadline_s=args.soak_duration_s,
                        prompt=args.prompt,
                        idle_gap_s=args.idle_gap,
                        allow_idle_completion=args.allow_idle_completion,
                        interval_s=args.soak_interval_s,
                        failure_limit=args.soak_failure_limit,
                        verbose=args.verbose,
                        serial_mod=serial_mod,
                        args=args,
                    )
                    aggregate_stats.append(stats)
                    results.append(aggregate_result(stats))
        finally:
            try:
                ser.close()
            except Exception:
                pass

    write_summary_files(log_dir, args, results, skipped, aggregate_stats)

    print(f"HIL artifacts: {log_dir}")
    verdict = final_verdict(results, args.dry_run)
    print(f"Final verdict: {verdict}")
    return exit_code_for_results(results, args.dry_run)


if __name__ == "__main__":
    raise SystemExit(run(sys.argv[1:]))

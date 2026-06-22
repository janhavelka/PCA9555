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

    @property
    def command_id(self) -> str:
        cleaned = re.sub(r"[^A-Za-z0-9]+", "_", self.command.strip()).strip("_")
        return cleaned.lower() or "command"


@dataclasses.dataclass
class CommandResult:
    command: str
    purpose: str
    serial_result: str
    operator_result: str
    completion_reason: str
    elapsed_s: float
    notes: str
    evidence: list[str]
    transcript_path: str | None = None


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
        purpose="Capture CLI command surface in the transcript.",
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
        completion_tokens=("=== Stress Results ===",),
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
        command="selftest",
        purpose="Run CLI API self-test that mutates output, direction, and polarity state.",
        expected=(r"Selftest result:", r"fail=0"),
        timeout_s=25.0,
        completion_tokens=("Selftest result:",),
        destructive=True,
        requires_opt_in="--include-output-tests",
        recovery_command="dirin 0xFFFF",
        classifier="selftest",
        notes=(
            "The CLI labels this safe, but it changes PCA9555 latches, direction, "
            "and polarity before restoring. Use only on a known-safe fixture."
        ),
    ),
    CommandSpec(
        command="stress 1000",
        purpose="Longer read-only stress soak.",
        expected=(r"=== Stress Results ===", r"fail=0"),
        timeout_s=240.0,
        completion_tokens=("=== Stress Results ===",),
        requires_opt_in="--include-soak",
        classifier="stress",
        notes="Longer soak is optional and still clears input interrupt state.",
    ),
    CommandSpec(
        command="stress_mix 100",
        purpose="Mixed read/write/config/polarity/mask stress test.",
        expected=(r"=== stress_mix summary ===", r"fail=0"),
        timeout_s=180.0,
        completion_tokens=("=== stress_mix summary ===",),
        destructive=True,
        requires_opt_in="--include-output-tests",
        recovery_command="dirin 0xFFFF",
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
    r"\bFAILED\b",
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
            (candidate / "transcripts").mkdir(exist_ok=False)
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
    parser.add_argument("--timeout", type=float, default=5.0, help="Default command timeout.")
    parser.add_argument("--dry-run", action="store_true", help="Plan only; do not open serial.")
    parser.add_argument("--commands", help="Optional text or JSON command file.")
    parser.add_argument("--address", default=DEFAULT_ADDRESS, help="Expected I2C address.")
    parser.add_argument("--startup-timeout", type=float, default=30.0)
    parser.add_argument("--idle-gap", type=float, default=0.5)
    parser.add_argument("--include-output-tests", action="store_true")
    parser.add_argument("--include-soak", action="store_true")
    parser.add_argument("--include-fault-tests", action="store_true")
    parser.add_argument(
        "--prompt",
        default=PROMPT_TOKEN,
        help="CLI prompt token. Default matches examples/common/CliStyle.h.",
    )
    return parser.parse_args(argv)


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
            custom_command_spec(command, default_timeout)
            for command in commands
        ]

    specs: list[CommandSpec] = []
    if not isinstance(loaded, list):
        raise ValueError("--commands JSON must contain a list")
    for entry in loaded:
        if isinstance(entry, str):
            specs.append(custom_command_spec(entry, default_timeout))
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
    return CommandSpec(
        command=spec.recovery_command,
        purpose=f"Restore-safe-state command after `{spec.command}`.",
        expected=(r"Status:\s+OK",),
        timeout_s=5.0,
        destructive=True,
        requires_opt_in=spec.requires_opt_in,
        classifier="write",
        notes=(
            "Automatic recovery command from HIL metadata. Restores all pins to "
            "input; output-to-input changes can trigger PCA9555 INT behavior."
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
    return CommandSpec(
        command=command,
        purpose="Custom operator-provided command.",
        timeout_s=default_timeout,
        classifier="custom",
        destructive=unsafe,
        requires_opt_in=requires_opt_in,
        notes=(
            "Custom command matched the unsafe CLI allowlist and requires opt-in."
            if unsafe
            else "Custom read-only stress exceeds the default safe cycle count and requires soak opt-in."
            if requires_opt_in == "--include-soak"
            else "Custom command classification is conservative."
        ),
    )


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
    if args.commands:
        custom_specs: list[CommandSpec] = []
        for spec in load_command_file(pathlib.Path(args.commands), args.timeout):
            custom_specs.append(spec)
            recovery = recovery_spec_for(spec)
            if recovery is not None:
                custom_specs.append(recovery)
        return custom_specs
    address_token = address_table_token(args.address)
    default_specs: list[CommandSpec] = []
    for spec in DEFAULT_SAFE_COMMANDS:
        if spec.command == "scan":
            default_specs.append(
                dataclasses.replace(
                    spec,
                    expected=(r"Scan complete", rf"(?m)^[0-7][0-9A-F]:\s+.*\b{re.escape(address_token)}\b"),
                )
            )
        else:
            default_specs.append(spec)
    all_specs: list[CommandSpec] = [*default_specs]
    for spec in OPTIONAL_COMMANDS:
        all_specs.append(spec)
        recovery = recovery_spec_for(spec)
        if recovery is not None:
            all_specs.append(recovery)
    return all_specs


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


def read_until(
    ser,
    *,
    prompt: str,
    timeout_s: float,
    idle_gap_s: float,
    completion_tokens: tuple[str, ...] = (),
) -> tuple[str, str]:
    deadline = time.monotonic() + timeout_s
    chunks: list[bytes] = []
    last_rx = time.monotonic()
    saw_output = False

    while time.monotonic() < deadline:
        waiting = getattr(ser, "in_waiting", 0)
        if waiting:
            data = ser.read(waiting)
            chunks.append(data)
            last_rx = time.monotonic()
            saw_output = True
            decoded = b"".join(chunks).decode("utf-8", errors="replace")
            if decoded.endswith(prompt):
                return decoded, "prompt"
            for token in completion_tokens:
                if token and token in decoded:
                    # Most CLI commands print the prompt after the token. Wait briefly for it.
                    prompt_deadline = min(deadline, time.monotonic() + idle_gap_s)
                    while time.monotonic() < prompt_deadline:
                        waiting_after = getattr(ser, "in_waiting", 0)
                        if waiting_after:
                            more = ser.read(waiting_after)
                            chunks.append(more)
                            decoded = b"".join(chunks).decode("utf-8", errors="replace")
                            if decoded.endswith(prompt):
                                return decoded, "prompt"
                        time.sleep(0.02)
                    return decoded, "completion_token"
        elif saw_output and (time.monotonic() - last_rx) >= idle_gap_s:
            return b"".join(chunks).decode("utf-8", errors="replace"), "idle"
        else:
            time.sleep(0.02)

    return b"".join(chunks).decode("utf-8", errors="replace"), "timeout"


def run_serial_command(
    ser,
    spec: CommandSpec,
    *,
    prompt: str,
    idle_gap_s: float,
    transcript_path: pathlib.Path,
) -> tuple[CommandResult, str]:
    start = time.monotonic()
    ser.write((spec.command + "\n").encode("utf-8"))
    ser.flush()
    raw, reason = read_until(
        ser,
        prompt=prompt,
        timeout_s=spec.timeout_s,
        idle_gap_s=idle_gap_s,
        completion_tokens=spec.completion_tokens,
    )
    elapsed = time.monotonic() - start
    transcript_path.write_text(strip_ansi(raw), encoding="utf-8")
    normalized = strip_ansi(raw)
    if normalized.endswith(prompt):
        normalized = normalized[: -len(prompt)]
    result, evidence = classify(spec, normalized, reason)
    operator_result = "OPERATOR_CHECK_REQUIRED" if spec.operator_check else "N/A"
    command_result = CommandResult(
            command=spec.command,
            purpose=spec.purpose,
            serial_result=result,
            operator_result=operator_result,
            completion_reason=reason,
            elapsed_s=elapsed,
            notes=spec.notes,
            evidence=evidence,
            transcript_path=str(transcript_path),
        )
    return command_result, raw


def skipped_result(spec: CommandSpec, result: str, reason: str) -> CommandResult:
    operator_result = "OPERATOR_CHECK_REQUIRED" if spec.operator_check else "N/A"
    return CommandResult(
        command=spec.command,
        purpose=spec.purpose,
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


def write_transcript_header(path: pathlib.Path, args: argparse.Namespace, command_count: int) -> None:
    header = (
        f"PCA9555 I2C HIL serial transcript\n"
        f"timestamp: {_dt.datetime.now().isoformat(timespec='seconds')}\n"
        f"port: {args.port or 'N/A'}\n"
        f"baud: {args.baud}\n"
        f"dry_run: {args.dry_run}\n"
        f"commands: {command_count}\n\n"
    )
    path.write_text(header, encoding="utf-8")


def append_transcript(path: pathlib.Path, command: str, raw_or_note: str) -> None:
    with path.open("a", encoding="utf-8", errors="replace") as handle:
        handle.write(f"\n===== COMMAND: {command} =====\n")
        handle.write(raw_or_note)
        if not raw_or_note.endswith("\n"):
            handle.write("\n")


def write_operator_checklist(path: pathlib.Path, skipped: list[CommandSpec]) -> None:
    lines = [
        "# PCA9555 HIL Operator Checklist",
        "",
        "Manual and visual checks are not serial PASS results. Mark them only after evidence is attached.",
        "",
        "## Manual Checks",
        "",
        "| Check | Purpose | Required Evidence | Result |",
        "| --- | --- | --- | --- |",
    ]
    for spec in MANUAL_CHECKS:
        lines.append(f"| `{spec.command}` | {spec.purpose} | {spec.notes} | OPERATOR_CHECK_REQUIRED |")
    lines.extend(["", "## Skipped Unsafe Or Opt-In Commands", ""])
    lines.extend(["| Command | Required Opt-In | Notes |", "| --- | --- | --- |"])
    if skipped:
        for spec in skipped:
            lines.append(f"| `{spec.command}` | `{spec.requires_opt_in or 'manual'}` | {spec.notes} |")
    else:
        lines.append("| None | N/A | All requested opt-in commands were included. |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_summary_files(
    log_dir: pathlib.Path,
    args: argparse.Namespace,
    results: list[CommandResult],
    skipped: list[CommandSpec],
    artifact_paths: dict[str, str],
) -> None:
    branch = git_output(["branch", "--show-current"])
    commit = git_output(["rev-parse", "HEAD"])
    status_short = git_output(["status", "--short"])
    verdict = final_verdict(results, args.dry_run)
    now = _dt.datetime.now().isoformat(timespec="seconds")
    data = {
        "timestamp": now,
        "repo": str(pathlib.Path.cwd()),
        "branch": branch,
        "commit": commit,
        "worktree_status": status_short if status_short else "clean",
        "serial_port": args.port,
        "baud": args.baud,
        "i2c_address": args.address,
        "dry_run": args.dry_run,
        "final_verdict": verdict,
        "pyserial_install_hint": PYSERIAL_HINT,
        "artifacts": artifact_paths,
        "commands": [dataclasses.asdict(result) for result in results],
        "skipped_opt_in_commands": [dataclasses.asdict(spec) for spec in skipped],
        "manual_checks": [dataclasses.asdict(spec) for spec in MANUAL_CHECKS],
        "identity_note": (
            "I2C ACK and scan results prove only address response. PCA9555 has no "
            "documented chip ID register, so this runner does not claim chip identity."
        ),
    }
    (log_dir / "summary.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# PCA9555 I2C HIL Summary",
        "",
        f"- Date/time: {now}",
        f"- Branch: `{branch}`",
        f"- Commit: `{commit}`",
        f"- Worktree: `{status_short if status_short else 'clean'}`",
        f"- Serial port: `{args.port or 'N/A'}`",
        f"- Baud: `{args.baud}`",
        f"- I2C address: `{args.address}`",
        f"- Dry run: `{args.dry_run}`",
        f"- Final verdict: `{verdict}`",
        "",
        "## Command Sequence",
        "",
        "| Command | Purpose | Serial Result | Operator Result | Completion | Elapsed | Notes |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for result in results:
        notes = result.notes.replace("|", "/") if result.notes else ""
        lines.append(
            f"| `{result.command}` | {result.purpose} | `{result.serial_result}` | "
            f"`{result.operator_result}` | `{result.completion_reason}` | "
            f"{result.elapsed_s:.2f}s | {notes} |"
        )
    lines.extend(["", "## Evidence Excerpts", ""])
    for result in results:
        if result.serial_result in {"FAIL", "TIMEOUT", "SERIAL_OK_OR_REVIEW", "REVIEW_REQUIRED"}:
            lines.append(f"### `{result.command}`")
            if result.evidence:
                for item in result.evidence:
                    lines.append(f"- `{item}`")
            else:
                lines.append("- No concise serial evidence captured; inspect transcript.")
            lines.append("")
    lines.extend(
        [
            "## Artifacts",
            "",
            f"- Serial transcript: `{artifact_paths['serial_transcript']}`",
            f"- Operator checklist: `{artifact_paths['operator_checklist']}`",
            f"- Machine summary: `{artifact_paths['summary_json']}`",
            "",
            "## Identity And Hardware Claim Guard",
            "",
            "No physical HIL validation is implied by a dry run. A scan or probe proves only I2C ACK at the address, not PCA9555 identity.",
        ]
    )
    (log_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def run(argv: list[str]) -> int:
    args = parse_args(argv)
    log_dir = create_log_dir(pathlib.Path(args.out))
    transcript_path = log_dir / "serial_transcript.txt"
    checklist_path = log_dir / "operator_checklist.md"
    summary_json_path = log_dir / "summary.json"
    write_transcript_header(transcript_path, args, 0)

    specs = build_command_sequence(args)
    skipped: list[CommandSpec] = []
    results: list[CommandResult] = []
    command_count = sum(
        1
        for spec in specs
        if not spec.operator_check and not ((spec.destructive or spec.requires_opt_in) and not opt_in_enabled(spec, args))
    )

    write_transcript_header(transcript_path, args, command_count)

    if args.dry_run:
        for spec in specs:
            if spec.operator_check:
                results.append(skipped_result(spec, "OPERATOR_CHECK_REQUIRED", "manual"))
            elif (spec.destructive or spec.requires_opt_in) and not opt_in_enabled(spec, args):
                skipped.append(spec)
                results.append(skipped_result(spec, "SKIPPED_UNSAFE", spec.requires_opt_in or "manual"))
            else:
                note = f"[DRY-RUN] would send: {spec.command}\n"
                append_transcript(transcript_path, spec.command, note)
                results.append(skipped_result(spec, "SKIPPED_DRY_RUN", "dry_run"))
    else:
        if not args.port:
            print("--port is required unless --dry-run is used", file=sys.stderr)
            return 2
        serial_mod = import_serial_module()
        if serial_mod is None:
            return 2
        try:
            ser = serial_mod.Serial(args.port, args.baud, timeout=0.05)
            ser.dtr = False
            ser.rts = False
        except Exception as exc:  # pragma: no cover - hardware path
            print(f"failed to open serial port {args.port}: {exc}", file=sys.stderr)
            return 2
        with ser:  # pragma: no cover - hardware path
            startup_raw, startup_reason = read_until(
                ser,
                prompt=args.prompt,
                timeout_s=args.startup_timeout,
                idle_gap_s=args.idle_gap,
            )
            append_transcript(transcript_path, "startup", startup_raw)
            if startup_reason == "timeout":
                results.append(
                    CommandResult(
                        command="startup",
                        purpose="Wait for CLI startup prompt.",
                        serial_result="TIMEOUT",
                        operator_result="N/A",
                        completion_reason="timeout",
                        elapsed_s=args.startup_timeout,
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
                command_index = 0
                for spec in specs:
                    if spec.operator_check:
                        results.append(skipped_result(spec, "OPERATOR_CHECK_REQUIRED", "manual"))
                        continue
                    if (spec.destructive or spec.requires_opt_in) and not opt_in_enabled(spec, args):
                        skipped.append(spec)
                        results.append(skipped_result(spec, "SKIPPED_UNSAFE", spec.requires_opt_in or "manual"))
                        continue
                    command_index += 1
                    if hasattr(ser, "reset_input_buffer"):
                        ser.reset_input_buffer()
                    command_transcript = log_dir / "transcripts" / f"{command_index:02d}_{spec.command_id}.txt"
                    result, raw_segment = run_serial_command(
                        ser,
                        spec,
                        prompt=args.prompt,
                        idle_gap_s=args.idle_gap,
                        transcript_path=command_transcript,
                    )
                    append_transcript(transcript_path, spec.command, raw_segment)
                    results.append(result)

    write_operator_checklist(checklist_path, skipped)
    artifacts = {
        "log_dir": str(log_dir),
        "serial_transcript": str(transcript_path),
        "operator_checklist": str(checklist_path),
        "summary_json": str(summary_json_path),
        "summary_md": str(log_dir / "summary.md"),
    }
    write_summary_files(log_dir, args, results, skipped, artifacts)

    checklist_text = checklist_path.read_text(encoding="utf-8")
    print(checklist_text)
    print(f"HIL artifacts: {log_dir}")
    print(f"Final verdict: {final_verdict(results, args.dry_run)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(run(sys.argv[1:]))

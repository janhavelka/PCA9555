#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_DIRS = ("src", "include")
VALID_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}

FORBIDDEN_TOKENS = {
    "Wire": re.compile(r"\bWire\b"),
    "TwoWire": re.compile(r"\bTwoWire\b"),
    "freertos/": re.compile(r"freertos/"),
    "driver/i2c": re.compile(r"driver/i2c"),
    "esp_": re.compile(r"\besp_"),
    "millis(": re.compile(r"\bmillis\s*\("),
    "micros(": re.compile(r"\bmicros\s*\("),
    "delay(": re.compile(r"\bdelay\s*\("),
    "delayMicroseconds(": re.compile(r"\bdelayMicroseconds\s*\("),
    "yield(": re.compile(r"\byield\s*\("),
    "vTaskDelay": re.compile(r"\bvTaskDelay\b"),
    "Serial": re.compile(r"\bSerial\b"),
    "String": re.compile(r"\bString\b"),
}

BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")
STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
FORBIDDEN_INCLUDES = {
    "Arduino.h": re.compile(r'^\s*#\s*include\s*[<"]Arduino\.h[>"]', re.MULTILINE),
    "Wire.h": re.compile(r'^\s*#\s*include\s*[<"]Wire\.h[>"]', re.MULTILINE),
    "freertos/": re.compile(r'^\s*#\s*include\s*[<"][^>"]*freertos/', re.MULTILINE),
    "driver/i2c": re.compile(r'^\s*#\s*include\s*[<"][^>"]*driver/i2c', re.MULTILINE),
}


def strip_non_code(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub("", text)
    text = LINE_COMMENT_RE.sub("", text)
    return STRING_RE.sub('""', text)


def collect_sources() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for dirname in SCAN_DIRS:
        root = ROOT / dirname
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in VALID_SUFFIXES:
                files.append(path)
    return files


def main() -> int:
    violations: list[str] = []

    for path in collect_sources():
        rel = path.relative_to(ROOT).as_posix()
        raw = path.read_text(encoding="utf-8", errors="replace")
        code = strip_non_code(raw)

        matches: dict[str, int] = {}
        for token_name, pattern in FORBIDDEN_INCLUDES.items():
            count = len(pattern.findall(raw))
            if count > 0:
                matches[token_name] = count
        for token_name, pattern in FORBIDDEN_TOKENS.items():
            count = len(pattern.findall(code))
            if count > 0:
                matches[token_name] = count
        if matches:
            violations.append(f"{rel} -> {matches}")

    if violations:
        print("Core framework guard FAILED:")
        for err in violations:
            print(f"- {err}")
        return 1

    print("Core framework guard PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Pack the library, enforce its public contents, and build a clean consumer."""

from __future__ import annotations

import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parent.parent
CONSUMER = ROOT / "tools" / "package_consumer.cpp"

REQUIRED_FILES = {
    "CHANGELOG.md",
    "CMakeLists.txt",
    "CONTRIBUTING.md",
    "Doxyfile",
    "LICENSE",
    "README.md",
    "SECURITY.md",
    "docs/README.md",
    "docs/chip_notes.md",
    "docs/datasheet_extraction.md",
    "docs/espidf.md",
    "docs/hardware_validation.md",
    "docs/register_reference.md",
    "examples/01_basic_bringup_cli/main.cpp",
    "examples/espidf_basic/main/main.cpp",
    "idf_component.yml",
    "include/PCA9555/CommandTable.h",
    "include/PCA9555/Config.h",
    "include/PCA9555/PCA9555.h",
    "include/PCA9555/Status.h",
    "include/PCA9555/Version.h",
    "library.json",
    "src/PCA9555.cpp",
}

FORBIDDEN_FILES = {
    ".gitignore",
    "AGENTS.md",
    "build_output.txt",
    "build_result.txt",
    "platformio.ini",
}

FORBIDDEN_PREFIXES = (
    ".github/",
    ".pio/",
    ".vscode/",
    "docs/doxygen/",
    "scripts/",
    "test/",
    "tests/",
    "tools/",
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def archive_members(archive: Path) -> set[str]:
    with tarfile.open(archive, "r:gz") as package:
        return {
            member.name.rstrip("/")
            for member in package.getmembers()
            if member.isfile()
        }


def validate_members(members: set[str]) -> None:
    missing = sorted(REQUIRED_FILES - members)
    if missing:
        fail("Package is missing required files: " + ", ".join(missing))

    forbidden = sorted(
        path
        for path in members
        if path in FORBIDDEN_FILES or path.startswith(FORBIDDEN_PREFIXES)
    )
    if forbidden:
        fail("Package contains internal files: " + ", ".join(forbidden))


def extract_checked(archive: Path, destination: Path) -> None:
    with tarfile.open(archive, "r:gz") as package:
        for member in package.getmembers():
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts:
                fail(f"Package contains unsafe path: {member.name}")
            target = destination.joinpath(*path.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if not member.isfile():
                fail(f"Package contains unsupported entry: {member.name}")
            source = package.extractfile(member)
            if source is None:
                fail(f"Package file cannot be read: {member.name}")
            target.parent.mkdir(parents=True, exist_ok=True)
            with source, target.open("wb") as output:
                shutil.copyfileobj(source, output)


def find_compiler() -> str:
    configured = os.environ.get("CXX")
    candidates = [configured] if configured else []
    candidates.extend(["c++", "g++", "clang++"])
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    fail("No C++ compiler found; set CXX to a C++17 compiler")
    raise AssertionError("unreachable")


def compile_consumer(package_root: Path, output: Path) -> None:
    compiler = find_compiler()
    command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror=return-type",
        f"-I{package_root / 'include'}",
        str(package_root / "src" / "PCA9555.cpp"),
        str(CONSUMER),
        "-o",
        str(output),
    ]
    subprocess.run(command, cwd=package_root, check=True)
    subprocess.run([str(output)], cwd=package_root, check=True)


def main() -> int:
    manifest = json.loads((ROOT / "library.json").read_text(encoding="utf-8"))
    archive_name = f"{manifest['name']}-{manifest['version']}.tar.gz"

    with tempfile.TemporaryDirectory(prefix="pca9555-package-") as temp_name:
        temp = Path(temp_name)
        subprocess.run(
            [
                sys.executable,
                "-m",
                "platformio",
                "pkg",
                "pack",
                str(ROOT),
                "--output",
                str(temp),
            ],
            cwd=ROOT,
            check=True,
        )
        archive = temp / archive_name
        if not archive.is_file():
            fail(f"Expected package archive was not created: {archive}")

        members = archive_members(archive)
        validate_members(members)

        unpacked = temp / "unpacked"
        unpacked.mkdir()
        extract_checked(archive, unpacked)
        executable = temp / (
            "package_consumer.exe" if os.name == "nt" else "package_consumer"
        )
        compile_consumer(unpacked, executable)

    print("Package content and clean consumer build PASSED")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError, tarfile.TarError) as error:
        print(f"check_package: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)

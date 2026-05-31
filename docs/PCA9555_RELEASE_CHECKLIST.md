# PCA9555 Release Checklist

This checklist separates merge readiness, release-candidate readiness, and
production/industry-grade claim readiness. Passing compile and native-test gates
does not imply hardware validation.

## Current Branch Status

- Branch under review: `hardening/pca9555-industry-readiness`
- Version source of truth: `library.json`
- Current declared version: `1.1.0`
- Version bump decision: deferred for merge; decide before tagging
- Hardware validation: not recorded
- Pure ESP-IDF local build: pending unless `idf.py` or CI evidence is available

Copy/move deletion is a source-compatibility change. If the deleted copy/move
operations remain, a major version or explicit compatibility decision is
required before release.

## Merge Readiness Gate

Required before merging this hardening branch:

| Gate | Command / Evidence | Required Result | Current Status |
| --- | --- | --- | --- |
| Branch and dirty-state review | `git status --short` and code review | Only intended files are changed | Required |
| Core framework guard | `python tools/check_core_timing_guard.py` | PASS | Required |
| Generated version check | `python scripts/generate_version.py check` | PASS | Required |
| ESP-IDF example contract guard | `python tools/check_idf_example_contract.py` | PASS | Required |
| Native tests | `python -m platformio test -e native` | PASS | Required |
| Arduino ESP32-S2 compile | `python -m platformio run -e esp32s2dev` | PASS | Required |
| Arduino ESP32-S3 compile | `python -m platformio run -e esp32s3dev` | PASS | Required |
| Documentation updated | README, Doxygen comments, register reference, changelog | Changed behavior documented honestly | Required |
| Release gates present | This file | Merge, RC, and production-claim gates documented | Required |
| Hardware matrix present | `docs/PCA9555_HARDWARE_VALIDATION_MATRIX.md` | All required tests listed and unrun tests marked honestly | Required |
| No unintended artifacts | `git status --short` after `pio pkg pack` | No generated package tarball or build output committed unintentionally | Remove package artifact after validation |

## Release Candidate Gate

Required before tagging a release candidate:

| Gate | Command / Evidence | Required Result |
| --- | --- | --- |
| Package validation | `python -m platformio pkg pack` | PASS; remove generated tarball unless intentionally publishing it |
| Arduino examples compile | `python -m platformio run -e esp32s2dev` and `python -m platformio run -e esp32s3dev` | PASS |
| Native tests and guards | Core guard, ESP-IDF contract guard, version check, native tests | PASS |
| Pure ESP-IDF build | `idf.py -C examples/espidf_basic set-target esp32s3 build` and `idf.py -C examples/espidf_basic set-target esp32s2 build` or CI logs | PASS, or explicitly marked pending for RC |
| CI | `.github/workflows/ci.yml` | All configured jobs pass, including ESP-IDF example matrix |
| API migration notes | README, Doxygen, changelog, release notes | New APIs and changed behavior documented |
| Changelog prepared | `CHANGELOG.md` | Unreleased section accurately lists hardening changes |
| Version decision documented | `library.json`, `idf_component.yml`, changelog, release notes | Version is deliberate and synchronized |
| Generated version synchronized | `python scripts/generate_version.py check` | PASS after any version change |
| Release notes added | `docs/releases/vX.Y.Z.md` | Present for the selected version |

## Production / Industry-Grade Claim Gate

Do not claim production-grade, industry-grade, field-proven, or fully validated
status until all required hardware evidence is recorded and reviewed.

Minimum required evidence:

| Gate | Required Evidence |
| --- | --- |
| Hardware matrix complete | Every required row in `docs/PCA9555_HARDWARE_VALIDATION_MATRIX.md` has PASS or justified FAIL with evidence |
| Real PCA9555 board at 100 kHz | Command log and fixture notes showing successful operation |
| Real PCA9555 board at 400 kHz | Command log and fixture notes showing successful operation |
| INT behavior | Active-low/open-drain pull-up, port 0 clear, port 1 clear, and both-port clear validated |
| Errata workaround | Logic analyzer or equivalent bus decode proving pointer park after input reads on shared bus |
| Output preload | Logic analyzer or safe-load evidence proving latch write before direction enable |
| Fault handling | Wrong address/NACK, unplug/replug, and brownout/power-cycle recovery logs |
| Long shared-bus soak | Duration, bus topology, error counters, and final health state recorded |
| Arduino hardware path | ESP32-S2 and ESP32-S3 hardware logs, or documented board-specific exclusion |
| ESP-IDF hardware path | ESP-IDF example or equivalent native IDF app tested on target hardware |

Allowed wording before this gate passes:

- production-oriented
- industry-readiness hardened
- pre-production candidate pending hardware/fault validation

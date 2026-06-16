# Release Checklist

This checklist separates merge readiness, release-candidate readiness, and
production/field-validation claims. Passing compile and native-test gates does
not imply hardware validation.

## Current Status

- Version source of truth: `library.json`
- Generated version header: `include/PCA9555/Version.h`
- Hardware validation: not recorded
- Pure ESP-IDF local build: pending unless `idf.py` or CI evidence is available
- Version bump decision: decide before tagging

Copy/move deletion is source-compatibility significant. If the deleted
copy/move operations remain, make an explicit compatibility/version decision
before release.

## Merge Readiness

Required before merging a hardening branch:

| Gate | Command / evidence | Required result |
| --- | --- | --- |
| Worktree review | `git status --short` and code review | Only intended files changed |
| Core framework guard | `python tools/check_core_timing_guard.py` | PASS |
| Generated version check | `python scripts/generate_version.py check` | PASS |
| ESP-IDF example contract | `python tools/check_idf_example_contract.py` | PASS |
| HIL/docs contract | `python tools/check_hil_contract.py` | PASS |
| Native tests | `python -m platformio test -e native` | PASS |
| Arduino ESP32-S2 compile | `python -m platformio run -e esp32s2dev` | PASS |
| Arduino ESP32-S3 compile | `python -m platformio run -e esp32s3dev` | PASS |
| Docs updated | README, Doxygen comments, `docs/`, changelog | Changed behavior documented honestly |
| No generated artifacts | `git status --short` after package/build checks | No unintended build/package output committed |

## Release Candidate

Required before tagging a release candidate:

| Gate | Command / evidence | Required result |
| --- | --- | --- |
| Package validation | `python -m platformio pkg pack` | PASS; remove generated tarball unless intentionally publishing it |
| Native tests and guards | Core guard, IDF contract, HIL contract, version check, native tests | PASS |
| Arduino examples compile | ESP32-S2 and ESP32-S3 PlatformIO builds | PASS |
| Pure ESP-IDF build | `idf.py -C examples/espidf_basic set-target esp32s3 build` and ESP32-S2 equivalent, or CI logs | PASS or explicitly deferred for RC |
| CI | `.github/workflows/ci.yml` | All configured jobs pass |
| API migration notes | README, Doxygen, changelog | New APIs and behavior documented |
| Version synchronized | `library.json`, `idf_component.yml`, `CHANGELOG.md`, generated `Version.h` | Deliberate and consistent |

## Production Or Field-Validation Claim

Do not claim production-grade, industry-grade, field-proven, or fully hardware
validated status until the evidence in [hardware_validation.md](hardware_validation.md)
is recorded and reviewed.

Minimum evidence:

- real PCA9555 board at 100 kHz
- real PCA9555 board at 400 kHz
- INT assertion and clear behavior
- errata pointer-park I2C decode on a shared bus
- latch preload before output enable
- wrong-address/NACK, unplug/replug, and brownout recovery
- long shared-bus soak
- Arduino ESP32-S2 and ESP32-S3 hardware logs, or documented exclusions
- ESP-IDF hardware path logs, or documented exclusion

Allowed wording before this gate passes:

- production-oriented
- hardened for industry readiness
- pre-production candidate pending hardware and fault validation

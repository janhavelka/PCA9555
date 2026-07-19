# Release Checklist

This checklist separates source readiness, release readiness, and hardware or
field claims. A host test or compile result is not hardware evidence.

## Current status

- Intended version: `3.0.0`, a breaking passive-driver release.
- Version source of truth: `library.json`.
- Generated/synchronized metadata: `include/PCA9555/Version.h`,
  `idf_component.yml`, and Doxygen `PROJECT_NUMBER`.
- Continuous real-target HIL remains incomplete. The June 2026 ESP32-S3 USB CDC
  blocker is recorded in the
  [HIL validation summary](reports/hil-validation-summary-20260625.md).
- Do not tag the release until every required source gate below passes on the
  final reviewed tree.

## Source readiness

| Gate | Command or evidence | Required result |
| --- | --- | --- |
| Scope review | `git status --short` and diff review | Only intended source changes; no product-repository edits |
| Generated metadata | `python scripts/generate_version.py check` | All synchronized files current |
| Core framework/timing guard | `python tools/check_core_timing_guard.py` | PASS |
| CLI contracts | `python tools/check_cli_contract.py` and `python tools/check_idf_example_contract.py` | PASS |
| HIL contracts/parser | `python tools/check_hil_contract.py` and `python tools/test_run_i2c_hil_parser.py` | PASS |
| Native tests | `python -m platformio test -e native` | PASS |
| ESP32-S2 Arduino build | `python -m platformio run -e esp32s2dev` | PASS |
| ESP32-S3 Arduino build | `python -m platformio run -e esp32s3dev` | PASS |
| Packaged external consumer | `python tools/check_package.py` | Export allowlist and clean consumer compile PASS |
| API documentation | `doxygen Doxyfile` | Exit 0; review warnings |
| Documentation review | README, changelog, public headers, `docs/` | Lifecycle, bounds, failures, migration, and validation status agree |
| Clean artifacts | `git status --short` after validation | No generated build/package output added |

The native tests must cover more than happy-path register access. Review the
test names and assertions for:

- zero-I2C bind, failed replacement preservation, and zero-I2C detach;
- terminal transport byte counts and `WriteEffect` mapping;
- three-state observational health with no offline admission gate;
- safe output-latch-before-direction ordering;
- shadow invalidation and indeterminate write effects;
- apply and verify phase order, maximum transaction bounds, mismatches, and
  failures at each phase;
- wrap-safe whole-operation deadlines and per-poll transaction budgets;
- request-ID mismatch, cancellation, timeout, exactly-once result delivery, and
  pending-result admission blocking;
- input-read/pointer-park exclusivity, accepted-versus-not-attempted failed
  command handling, and cleanup after failure/cancel/timeout;
- odd-start register-pair behavior and raw Configuration-write rejection.

## Release candidate

Before creating a release candidate or final tag:

1. Run every source-readiness gate on the final commit.
2. Confirm CI passes Arduino builds, native tests, package consumption, Doxygen,
   and native ESP-IDF builds for ESP32-S2 and ESP32-S3.
3. Review the 2.x-to-3.0 migration section in README and the breaking-change
   list in CHANGELOG.
4. Confirm `library.json`, `idf_component.yml`, Doxygen, and generated
   `Version.h` all report `3.0.0`.
5. Confirm the public package does not contain tests, CI, internal audit files,
   transient logs, or repository-control files.
6. Tag only the reviewed commit as `v3.0.0`.

For external integrations, pin the exact tag or an immutable audited commit.
Do not point a production manifest at a moving branch.

## Hardware and field-readiness gate

Do not claim production-grade, industry-grade, field-proven, or fully hardware
validated status until the evidence in
[hardware_validation.md](hardware_validation.md) is recorded and reviewed.

Minimum evidence includes:

- PCA9555 operation at 100 kHz and 400 kHz;
- safe power-up, MCU-only reset, PCA9555-only brownout, and reconnect behavior;
- output latch preload before output enable under a logic analyzer;
- INT assertion/clear behavior and the nonzero pointer-park decode;
- cancellation/timeout cleanup at the input-read boundary;
- wrong-address, address-NACK, data-NACK where distinguishable, timeout, and
  uncertain-write behavior;
- a long soak with another active target through the real bus owner;
- Arduino ESP32-S2 and ESP32-S3 hardware logs;
- native ESP-IDF hardware logs, or a reviewed product exclusion.

Allowed wording before that gate passes:

- production-oriented;
- designed for deterministic owner integration;
- pre-production candidate pending hardware and fault validation.

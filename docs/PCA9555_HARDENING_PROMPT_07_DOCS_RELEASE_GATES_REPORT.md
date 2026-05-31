# PCA9555 Hardening Prompt 07 Docs Release Gates Report

## Scope

- Branch: `hardening/pca9555-industry-readiness`
- Prompt: `07_docs_release_gates_hardware_validation.md`
- Date: 2026-05-31

Prompt 07 was limited to documentation accuracy, release gates, and hardware
validation planning. No hardware commands were run and no hardware validation is
claimed.

## Start Checks

| Check | Result |
|---|---|
| `git status --short` | Dirty with related Prompt 01-06 hardening changes already present |
| `git branch --show-current` | `hardening/pca9555-industry-readiness` |
| Prompt 01 report exists | PASS |
| Prompt 02 report exists | PASS |
| Prompt 03 report exists | PASS |
| Prompt 04 report exists | PASS |
| Prompt 05 report exists | PASS |
| Prompt 06 report exists | PASS |

No unrelated dirty work was identified. No commit was made because this prompt
did not ask for commit/sync.

## Documentation Changed

- `README.md`
  - Reworked the top summary to describe the branch as production-oriented and
    pre-production pending hardware validation.
  - Added links to the release checklist, hardware validation matrix, and Prompt
    07 report.
  - Added a Safety and Electrical Notes section.
  - Clarified `probe()` as address response only, not chip-ID proof.
  - Clarified that `recover()` cannot force a true PCA9555 power-on reset.
  - Clarified local PlatformIO validation versus ESP-IDF release/CI gates.
  - Fixed bit-manipulation wording so `togglePin()` and legacy
    `configureOutputBits()` are not described as simple 2-byte burst helpers.
- `docs/register_reference.md`
  - Corrected Input Port reset values from fixed `0xFF` to pin-dependent.
  - Documented Output Port defaults as latch values `0xFFFF`.
  - Documented polarity defaults `0x0000` and configuration defaults `0xFFFF`.
  - Added pair auto-increment and odd-start wrap behavior.
  - Added interrupt and errata notes, including port-specific interrupt clear.
- `docs/PCA9555_RELEASE_CHECKLIST.md`
  - Added merge readiness, release-candidate, and production/industry-grade
    claim gates.
- `docs/PCA9555_HARDWARE_VALIDATION_MATRIX.md`
  - Added required hardware validation tests with every result marked `NOT RUN`.
- `CHANGELOG.md`
  - Updated the Unreleased section to reflect the hardening work.
  - Recorded that version bump decision and hardware validation remain pending.
- `docs/releases/v1.0.0.md`
  - Qualified historical release wording to avoid implying Prompt 07 hardware
    validation evidence.
- `include/PCA9555/PCA9555.h`
  - Updated Doxygen for `end()`, `probe()`, and `recover()` to match actual
    behavior and caveats.
- `include/PCA9555/Status.h`
  - Updated `IN_PROGRESS` Doxygen to match pass-through transport behavior.
- `docs/PCA9555_INDUSTRY_READINESS_AUDIT.md`
  - Added a note that the audit is a historical pre-hardening baseline and may
    intentionally contradict the current Prompt 01-07 state.

## Overclaims Removed Or Qualified

- README and package metadata do not claim production-grade, industry-grade,
  field-proven, or fully validated status.
- Historical audit/backlog mentions remain as warnings or baseline findings, not
  current product claims.
- The README now states that the library is a pre-production candidate until the
  hardware validation matrix is run and recorded on real target boards.
- ESP-IDF build wording now separates local static guard validation from
  `idf.py` release/CI build gates.

## Release Gates Added

`docs/PCA9555_RELEASE_CHECKLIST.md` defines:

- Merge readiness gate:
  - core guard clean,
  - generated version check clean,
  - native tests pass,
  - Arduino ESP32-S2 and ESP32-S3 builds pass,
  - ESP-IDF example contract guard passes,
  - docs/reports/checklists present,
  - no unintended generated artifacts.
- Release candidate gate:
  - package validation passes,
  - examples compile,
  - pure ESP-IDF build passes or is explicitly marked pending,
  - API changes and migration notes documented,
  - changelog prepared,
  - version bump decision documented and synchronized.
- Production/industry-grade claim gate:
  - hardware validation matrix complete,
  - real PCA9555 operation at 100 kHz and 400 kHz,
  - INT behavior validated,
  - errata workaround validated on a shared bus,
  - output preload verified with safe loads or logic analyzer,
  - NACK/unplug/replug/brownout tested,
  - long shared-bus soak tested,
  - Arduino and ESP-IDF target hardware paths tested.

## Hardware Matrix Added

`docs/PCA9555_HARDWARE_VALIDATION_MATRIX.md` includes all required Prompt 07
tests:

1. Wired address scan/probe for all intended addresses.
2. POR defaults.
3. Input reads on all 16 pins.
4. Output writes on all 16 pins through safe preload.
5. Bulk output mask write with unrelated pins unchanged.
6. Latch preload before direction change verified with logic analyzer or safe
   external loads.
7. Output-to-input transition false-interrupt observation/handling.
8. Polarity inversion.
9. INT assert and clear for port 0.
10. INT assert and clear for port 1.
11. INT assert and clear for both ports.
12. Errata workaround pointer-park on shared bus.
13. NACK / wrong address behavior.
14. Unplug/replug recovery.
15. Brownout/power-cycle recovery.
16. 100 kHz I2C operation.
17. 400 kHz I2C operation.
18. Long shared-bus soak.
19. Arduino ESP32-S2 hardware run.
20. Arduino ESP32-S3 hardware run.
21. ESP-IDF hardware run.

Every row is marked `NOT RUN` with evidence placeholders.

## Validation Results

| Command | Result |
|---|---|
| `python tools/check_core_timing_guard.py` | PASS: `Core framework guard PASSED` |
| `python tools/check_idf_example_contract.py` | PASS: `ESP-IDF example contract PASSED` |
| `python scripts/generate_version.py check` | PASS: `Version.h` up to date |
| `python -m platformio test -e native` | PASS: 126 tests, 126 succeeded, duration `00:00:07.731` |
| `python -m platformio run -e esp32s2dev` | PASS: `SUCCESS`, duration `00:00:39.968` |
| `python -m platformio run -e esp32s3dev` | PASS: `SUCCESS`, duration `00:00:36.296` |
| `python -m platformio pkg pack` | PASS: wrote `PCA9555-1.1.0.tar.gz`; generated tarball removed after validation |

## Commands Not Run

- No hardware commands were run.
- No upload or serial monitor commands were run.
- No local pure ESP-IDF `idf.py` build was run in Prompt 07. Prompt 06 already
  recorded that `idf.py` was unavailable locally; Prompt 07 release gates mark
  pure ESP-IDF build evidence as required before release unless explicitly
  marked pending for an RC.

## Remaining Hardware Tests

All hardware-validation rows remain `NOT RUN`:

- real board address/probe/default verification,
- all-pin input and safe output validation,
- logic-analyzer verification of latch preload ordering,
- INT per-port and both-port behavior,
- errata workaround on a shared bus,
- NACK/unplug/replug/brownout recovery,
- 100 kHz and 400 kHz operation,
- long shared-bus soak,
- Arduino ESP32-S2 and ESP32-S3 hardware runs,
- ESP-IDF hardware run.

## Subagent Findings

- `docs-accuracy-agent` found stale input register defaults, missing
  `probe()`/`recover()` caveats in Doxygen, stale audit wording, and ESP-IDF
  build wording that needed an evidence guard.
- `release-gates-agent` identified the need for explicit merge, RC, and
  production-claim gates, plus a SemVer decision because copy/move deletion is
  source-compatibility significant.
- `hardware-matrix-agent` provided the bench setup and safety baseline used to
  build the hardware validation matrix.
- `integration-review-agent` found README bit-manipulation wording that was too
  broad and `IN_PROGRESS` Doxygen that conflicted with the implemented
  transport pass-through behavior.

## Final Pre-Merge Recommendation

Proceed to Prompt 08 final integration review. This branch is a merge candidate
only after Prompt 08 confirms the full diff is scoped and all reports are
present.

Do not tag a release yet. The release version decision is still pending, local
pure ESP-IDF build evidence is absent, and hardware validation has not been run.
Do not claim production-grade or industry-grade readiness until the hardware
validation matrix has passing evidence.

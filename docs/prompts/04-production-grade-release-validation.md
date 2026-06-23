# AI Coder Prompt: Production-Grade Release Validation Pass

Repository: `PCA9555`

Absolute path: `C:\Users\Honza\Documents\Projects\PCA9555`

## Goal

Turn the current pre-production PCA9555 library into a release candidate that can
honestly be called production-grade only if the evidence supports that claim.

Do not treat compile success, native tests, an I2C ACK, or a short HIL run as
production validation. The previous COM5 8-hour run did not pass because the
host runner captured partial serial output. A fixed prompt-gated runner exists;
the release evidence must be regenerated with the current code and a clean,
recorded firmware build.

Prefer simple, functional, robust design. Reuse existing code, runner helpers,
docs, test fixtures, and CLI commands where feasible. Do not add a framework,
new bus manager, fake production device, broad abstraction, or placeholder API.

## Required Initial Read

Read these files before making any decision or edit:

- `AGENTS.md`
- `docs/release.md`
- `docs/hardware_validation.md`
- `docs/reports/hil-validation-COM5-20260623.md`
- `README.md`
- `CHANGELOG.md`
- `library.json`
- `idf_component.yml`
- `include/PCA9555/Status.h`
- `include/PCA9555/Config.h`
- `include/PCA9555/PCA9555.h`
- `src/PCA9555.cpp`
- `test/test_basic.cpp`
- `tools/run_i2c_hil.py`
- `tools/check_hil_contract.py`
- `tools/check_idf_example_contract.py`
- `examples/common/CommandHandler.h`
- `examples/espidf_basic/README.md`

Check `git status --short` first. Preserve dirty user changes. Do not commit
unless explicitly asked.

## Use Subagents

You may spawn read-only subagents for these independent audits:

- HIL/release evidence audit: docs, reports, release gates, artifact naming.
- Driver/API audit: public statuses, chunked jobs, dirty-state behavior,
  recovery/offline behavior, tests.
- Example/ESP-IDF audit: Arduino CLI parity, ESP-IDF contract, framework
  boundary, hardware run commands.

Keep final judgment, edits, hardware execution, report writing, and verification
in the main agent.

## Production-Grade Exit Criteria

All criteria below must pass before using production-grade, field-validated, or
fully hardware-validated wording:

1. Worktree and version evidence are recorded.
   - Record branch, commit, dirty/clean state, host OS, PlatformIO version,
     Python version, target board, serial port, firmware version banner, and
     hardware identifiers.
   - Firmware under test must be built from the same commit being reported.
   - If the worktree is dirty, list exact dirty files and explain whether they
     are part of the firmware image.

2. Static and host checks pass.
   - `python tools\test_run_i2c_hil_parser.py`
   - `python tools\check_hil_contract.py`
   - `python tools\check_cli_contract.py`
   - `python tools\check_core_timing_guard.py`
   - `python tools\check_idf_example_contract.py`
   - `python scripts\generate_version.py check`
   - `python -m py_compile tools\run_i2c_hil.py`
   - `git diff --check`

3. Native and Arduino builds pass.
   - `python -m platformio test -e native`
   - `python -m platformio run -e esp32s2dev`
   - `python -m platformio run -e esp32s3dev`
   - `python -m platformio pkg pack`
   - Remove generated `PCA9555-<version>.tar.gz` unless intentionally publishing
     it.

4. Pure ESP-IDF evidence exists.
   - Run native IDF builds for ESP32-S2 and ESP32-S3, or document the exact
     blocker if the local machine lacks ESP-IDF.
   - Preferred commands:

     ```powershell
     idf.py -C examples\espidf_basic set-target esp32s2
     idf.py -C examples\espidf_basic build
     idf.py -C examples\espidf_basic set-target esp32s3
     idf.py -C examples\espidf_basic build
     ```

   - Hardware ESP-IDF smoke evidence is required for production validation, or
     the final wording must remain pre-production.

5. A fresh 8-hour HIL run with the fixed runner passes.
   - Use prompt-gated completion. Do not use `--allow-idle-completion`.
   - Use current firmware, current commit, and cleanly recorded dirty state.
   - Minimum read-oriented soak duration: `28800` seconds.
   - Failure threshold for production sign-off: `0` host command failures and
     `0` review-classified partial captures.
   - Device health at end: `READY`, `consecutiveFailures == 0`,
     `totalFailures == 0`, `lastError` never or `Err::OK`.
   - Runner final verdict may be `OPERATOR_REVIEW_REQUIRED` until manual rows
     are complete, but serial/HIL command results must have no `FAIL`,
     `TIMEOUT`, or `SERIAL_OK_OR_REVIEW`.

6. Mutating/output HIL is run only on a proven safe fixture.
   - Safe fixture requirements:
     - Current-limited load per output, maximum recommended test load
       `<= 5 mA` per pin.
     - No external driver fights PCA9555 outputs.
     - VCC, pull-ups, address straps, INT pull-up, and unused input states are
       documented.
     - Operator attaches wiring photo and pin/load table.
   - Run output-changing CLI/API checks only after the fixture is documented.
   - Required commands or equivalent API tests: `selftest`, `alllow`, `allhigh`,
     `pattern`, `walk`, `sweep`, masked writes, `configureOutputs()`,
     `preloadOutput()`, `setDirection()`, and recovery `dirin 0xFFFF`.

7. INT and errata behavior are physically validated.
   - INT pull-up: document value. Recommended `10 kOhm` unless the board already
     provides a known value.
   - Capture INT assertion and clear for Port 0, Port 1, and both-port clear.
   - Capture I2C analyzer evidence that input reads are followed by command
     pointer park `cmd::ERRATA_SAFE_CMD` / `0x02`.
   - On shared bus, show no interleaving inside synchronous locked input-read
     plus pointer-park sequence.

8. Fault and recovery validation passes.
   - Wrong address / address NACK.
   - Data NACK or safe disconnect where feasible.
   - Unplug/replug to OFFLINE and manual `recover()`.
   - PCA9555-only brownout or power cycle, then `recover()`.
   - Confirm normal I/O is blocked while OFFLINE and `recover()` is the only
     public path that rechecks the bus while OFFLINE.

9. 100 kHz and 400 kHz evidence exists.
   - Run scan/probe/read/config/polarity/health and representative output tests
     at both frequencies.
   - End health must be `READY`, `consecutiveFailures == 0`, `totalFailures == 0`
     for the validation window.

10. Shared-bus evidence exists.
    - Include PCA9555 plus at least one other readable I2C target.
    - Run the long read-oriented soak and at least one shorter mutating safe
      output run while other-target traffic occurs through the application bus
      owner.
    - No unexplained I2C failures, hangs, resets, or INT loss.

## Concrete Names And Artifact Conventions

Use these names unless there is a compelling local reason not to:

- Main report:
  `docs/reports/hil-validation-COM5-YYYYMMDD-production-candidate.md`
- Runner summary copy:
  `docs/reports/hil-validation-COM5-YYYYMMDD-runner-summary.md`
- Artifact root:
  `hil_logs/production_candidate_YYYYMMDD/`
- Hardware checklist copy:
  `docs/reports/hardware-validation-COM5-YYYYMMDD-checklist.md`
- ESP-IDF report:
  `docs/reports/espidf-validation-YYYYMMDD.md`
- Release readiness note:
  `docs/reports/release-readiness-YYYYMMDD.md`

Use report status labels exactly:

- `BLOCKED_NO_HARDWARE`
- `BLOCKED_UNSAFE_FIXTURE`
- `BLOCKED_HIL_FAILURE`
- `BLOCKED_IDF_EVIDENCE`
- `PRE_PRODUCTION_CANDIDATE`
- `PRODUCTION_VALIDATED`

These labels are report strings only. Do not add a public library enum for them.

## Concrete Status And API Guidance

Do not add new public `Err` values or `DriverState` values unless a verified
bug cannot be represented by the current vocabulary.

Use existing public statuses consistently:

- Offline normal-operation block: `Err::BUSY`,
  message `"Driver is offline; call recover()"`.
- Dirty hardware/cache no-op block: `Err::BUSY`,
  message `"Hardware state dirty; call recover()"`.
- No cached input snapshot yet: `Err::BUSY`,
  message `"No input snapshot available"`.
- Not initialized: `Err::NOT_INITIALIZED`,
  message `"begin() not called"`.
- Invalid config/argument: `Err::INVALID_CONFIG` or `Err::INVALID_PARAM`.
- Address NACK during begin/probe: `Err::DEVICE_NOT_FOUND` where the existing
  contract maps address absence to device-not-found.
- Transport detail errors: preserve `Err::I2C_NACK_ADDR`,
  `Err::I2C_NACK_DATA`, `Err::I2C_TIMEOUT`, `Err::I2C_BUS`, or generic
  `Err::I2C_ERROR`.

If a new helper is necessary, prefer a private/local helper with a current
caller and focused test. Avoid new public API unless the validation finds a
real API-contract gap.

## HIL Commands

Build and upload current firmware first:

```powershell
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s3dev -t upload --upload-port COM5
```

Fresh read-oriented 8-hour run:

```powershell
python tools\run_i2c_hil.py --port COM5 --baud 115200 --address 0x20 --timeout-s 8 --idle-timeout-s 1.0 --boot-settle-s 2 --startup-timeout 20 --benchmark-command read --benchmark-count 50 --benchmark-warmup 3 --soak-duration-s 28800 --soak-command-mix read,outputs,config,polarity,health,probe --soak-interval-s 0.2 --soak-failure-limit 1 --out hil_logs\production_candidate_YYYYMMDD --report docs\reports\hil-validation-COM5-YYYYMMDD-runner-summary.md
```

Safe output run, only after the fixture is documented:

```powershell
python tools\run_i2c_hil.py --port COM5 --baud 115200 --address 0x20 --timeout-s 8 --idle-timeout-s 1.0 --boot-settle-s 2 --startup-timeout 20 --include-output-tests --include-soak --soak-duration-s 600 --soak-command-mix read,outputs,config,polarity,health,probe --soak-interval-s 0.2 --soak-failure-limit 1 --out hil_logs\production_candidate_outputs_YYYYMMDD
```

If any command times out or returns review-classified partial output, stop and
classify the report as `BLOCKED_HIL_FAILURE`. Do not hide this with reruns.
Reruns are allowed only after identifying and fixing the root cause; keep all
failed artifacts.

## Documentation Updates Required

Update docs only when evidence or behavior actually changes:

- `docs/hardware_validation.md`: fill evidence rows with result and artifact
  paths; leave unrun rows as `NOT RUN`.
- `docs/release.md`: update current status and release-candidate gates.
- `README.md`: update build/validation wording only if the claim level changes.
- `CHANGELOG.md`: record behavior/API/doc changes under the correct version.
- `library.json` and `idf_component.yml`: synchronize version only after the
  SemVer decision.
- `include/PCA9555/Version.h`: regenerate with
  `python scripts\generate_version.py update`; never edit by hand.

## SemVer Decision

Before tagging, decide version level explicitly:

- PATCH: bug fixes, docs, tooling, validation reports, no source-compatible API
  behavior change.
- MINOR: backward-compatible public API additions or new error codes appended to
  `Err`.
- MAJOR: source-incompatible public API change, changed enum values, renamed
  public APIs, or removed public APIs.

For the current state, expect at least a PATCH release if no further public API
changes are made. If this pass adds new public APIs or `Err` values, use MINOR
unless source compatibility is broken.

## Stop Conditions

Stop and report instead of editing around the problem when:

- Hardware fixture safety is unknown for output-driving tests.
- ESP-IDF tooling is missing and cannot be installed in the current environment.
- The 8-hour HIL produces any `FAIL`, `TIMEOUT`, or `SERIAL_OK_OR_REVIEW`.
- Device health shows any unexplained I2C failure during the production
  validation window.
- A required fix would need hidden retries, unbounded waits, bus ownership
  inside the library, or a broad abstraction.

## Final Response Requirements

Report:

- Whether production validation passed or remains blocked.
- Exact status label from this prompt.
- Files changed.
- HIL commands run and artifact paths.
- Native/build/static commands run and results.
- Hardware rows completed and rows still `NOT RUN`.
- Any code/API findings fixed, ordered by severity.
- Exact version decision and files updated, or why versioning was deferred.

Do not claim production-grade unless every production-grade exit criterion in
this prompt is satisfied with attached evidence.

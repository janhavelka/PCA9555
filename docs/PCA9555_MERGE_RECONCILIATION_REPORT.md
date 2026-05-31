# PCA9555 Merge Reconciliation Report

Date: 2026-05-31

Branch: `hardening/pca9555-industry-readiness`

Base branch/commit: `origin/main` at `747e5f73a922e3e41f7eb1d60dd5f94fd5ca0009`

Reviewed branch commit: `3852008c2f8ef0b2243404765ae5b9983665f80b`

Starting branch commit before reconciliation: `3c92063b23ed243d7ed27311bdd859509a09b137`

## Summary

The hardening branch was rebased onto `origin/main` and cleaned up to keep the permanent documentation useful. Prompt-internal audit and progress reports were removed from the branch result. The remaining docs now describe the public API, release gates, hardware validation expectations, ESP-IDF example usage, and optional Python HIL workflow in simple operational terms.

The branch is suitable for merge review after the remaining local limitations below are accepted. It is not ready for a release tag because hardware validation and pure ESP-IDF local builds were not completed in this environment, and the version/SemVer decision is still explicit release work.

## Rebase Result

Rebase command used:

```bash
git rebase origin/main
```

Rebased commits:

```text
89963de Add PCA9555 release checklist and example project for ESP-IDF
3852008 Add I2C HIL self-test runner
```

The rebase completed successfully.

## Conflict Resolutions

README and release documentation were reconciled to keep user-facing docs and remove prompt-process noise.

The canonical ESP-IDF example path is:

```text
examples/espidf_basic
```

The duplicate/stale path is intentionally absent:

```text
examples/esp_idf/basic
```

Reason: `examples/espidf_basic` is the path already carried by `origin/main`, so keeping it avoids unnecessary path churn.

## Documentation Cleanup

Kept permanent docs:

- `README.md`
- `CHANGELOG.md`
- `docs/register_reference.md`
- `docs/releases/v1.0.0.md`
- `docs/PCA9555_RELEASE_CHECKLIST.md`
- `docs/PCA9555_HARDWARE_VALIDATION_MATRIX.md`
- `docs/I2C_HIL_RUNBOOK.md`
- `docs/I2C_HIL_TARGET_TEMPLATE.md`
- `examples/espidf_basic/README.md`

Removed or kept out of the final branch result:

- prompt-by-prompt hardening reports
- temporary industry-readiness audit/backlog files
- `docs/PCA9555_INDUSTRY_HARDENING_FINAL_REPORT.md`
- `docs/I2C_HIL_SELFTEST_REPORT.md`
- generated `docs/doxygen/` output
- generated PlatformIO package archive `PCA9555-1.1.0.tar.gz`

The HIL self-test report was removed because it is a transient run artifact. The stable docs are the runbook and target template.

## API Documentation Work

Doxygen comments were cleaned up for the hardening API surface:

- copy and move operations are documented as deleted, non-owning-driver behavior;
- `Config::nowMs` now states that it is a monotonic millisecond callback used only for health timestamps;
- I2C lock hooks document the shared-bus serialization contract;
- dirty-state APIs document when cache and hardware may diverge;
- `recover()` documents OFFLINE behavior and reapply ordering;
- safe direction APIs document output-latch preload before configuration writes;
- interrupt and errata APIs document what is read, when the errata command is written, and what happens on failure.

## README And Changelog Work

`README.md` now points to the durable release and validation docs instead of prompt reports.

`CHANGELOG.md` now lists the hardening work by public API and behavior:

- dirty-state diagnostics;
- safe output preload and direction APIs;
- interrupt clear and errata APIs;
- shared-bus lock hooks;
- ESP-IDF component/example support;
- native fault tests;
- HIL runner and target template;
- release checklist and hardware validation matrix.

The changelog deliberately does not claim a completed release. Versioning remains deferred.

## Version Decision

Current version files remain at `1.1.0`.

The release checklist and changelog state that tagging requires an explicit compatibility/version decision. The deleted copy/move operations are source-compatibility significant, so the release manager should decide whether the next tag is a major release or whether the change is acceptable for the current pre-release branch policy.

## Validation Commands Run

Passed:

```bash
python tools/check_core_timing_guard.py
python tools/check_idf_example_contract.py
python scripts/generate_version.py check
python tools/check_hil_contract.py
python tools/run_i2c_hil.py --dry-run
doxygen Doxyfile
python -m platformio test -e native
python -m platformio run -e esp32s2dev
python -m platformio run -e esp32s3dev
python -m platformio pkg pack
```

Native test result:

```text
126 test cases: 126 succeeded
```

PlatformIO firmware build results:

```text
esp32s2dev SUCCESS
esp32s3dev SUCCESS
```

Package result:

```text
Wrote a tarball to PCA9555-1.1.0.tar.gz
```

The tarball was removed after the check.

First native test attempt:

```text
ERRORED: .pio\build\native\program.exe could not be opened because the generated native build directory was missing.
```

The generated directory was recreated and the same command then passed.

## Commands Not Run

Pure ESP-IDF builds were not run because `idf.py` is not available on PATH in this environment.

Observed command result:

```bash
idf.py --version
```

Result:

```text
idf.py : The term 'idf.py' is not recognized as the name of a cmdlet, function, script file, or operable program.
```

Not run for that reason:

```bash
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

GitHub CLI checks were not run because `gh` is not available on PATH:

```text
gh : The term 'gh' is not recognized as the name of a cmdlet, function, script file, or operable program.
```

No hardware validation was run. No field validation is claimed.

## CI Readiness

The branch has local guard coverage for:

- core framework dependency boundaries;
- ESP-IDF example contract and stale path detection;
- generated version consistency;
- HIL documentation and runner contract.

CI should still be treated as required merge evidence because pure ESP-IDF builds and remote CI status were not verified locally.

## Merge Verdict

Ready to merge after review of the documented release/version decision and acceptance that pure ESP-IDF and hardware validation remain unrun locally.

## Release Verdict

Not ready to release.

Reasons:

- no hardware validation was run;
- pure ESP-IDF builds were not run locally;
- the version/SemVer decision is not finalized;
- generated release artifacts are not kept or signed.

## Production Claim Verdict

May claim production-oriented / pre-production hardened only.

Do not claim field-proven production readiness or industry-grade validation until hardware validation, pure ESP-IDF builds, CI, and release packaging evidence are complete.

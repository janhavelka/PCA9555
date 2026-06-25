# PCA9555 Release Readiness - 2026-06-23

Status label: `BLOCKED_HIL_FAILURE`

Static, native, Arduino build, and package gates passed for the dirty tree, but
the required 8-hour HIL gate failed with serial `TIMEOUT` results. Production
validation and release-candidate sign-off are deferred.

Primary evidence:

- `docs/reports/hil-validation-COM5-20260623-production-candidate.md`
- `docs/reports/hil-validation-COM5-20260623-runner-summary.md`
- `hil_logs/production_candidate_20260623/i2c_20260623_181735`
- `hil_logs/production_candidate_20260623/i2c_20260623_202601`

Blocked items:

- Clean 8-hour prompt-gated HIL with zero `FAIL`, `TIMEOUT`, and `SERIAL_OK_OR_REVIEW`.
- Pure ESP-IDF local build or CI evidence; `idf.py` was not available in this shell.
- Safe output fixture evidence.
- INT, errata pointer-park, fault/recovery, 100 kHz, 400 kHz, and shared-bus evidence.

Version decision: deferred. No tag should be created from this evidence.

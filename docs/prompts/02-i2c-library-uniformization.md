# PCA9555 I2C Uniformization Prompt

Repository: `PCA9555`

Absolute path: `C:\Users\Honza\Documents\Projects\PCA9555`

## Execution Rules

You are working inside this single repository. Implement this prompt directly;
do not repeat the cross-repository audit.

You may spawn subagents for read-only inspection of APIs, tests, I2C
transactions, docs, and diagnostics. Keep final judgment, edits, and
verification in the main agent.

Prefer simple, robust, readable code. Before adding code, inspect whether
existing code can be simplified, reused, tightened, or deleted.

Preserve dirty user changes. Do not commit unless explicitly asked.

## Common Uniformization Target

Apply this shared I2C library contract: injected non-owning transport, `Status` returns, cache-only `getSettings(SettingsSnapshot&) const`, active `probe()`/diagnostics named explicitly, `DriverState` with `state()` and `driverState()`, `isOnline()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, and `totalSuccess()`.

Keep the common `Err` vocabulary append-only where missing: `OK`, `NOT_INITIALIZED`, `INVALID_CONFIG`, `INVALID_PARAM`, `I2C_ERROR`, `I2C_NACK_ADDR`, `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, `DEVICE_NOT_FOUND`, `TIMEOUT`, `BUSY`, and `IN_PROGRESS`. Preserve PCA9555-specific register mismatch, output/config cache, and `hardwareStateDirty()` behavior.

Uniformization is not a new base class or framework. Make only local, source-compatible additions and tests.

## Current State

- Public lifecycle and health are in `include\PCA9555\PCA9555.h`: `SettingsSnapshot` starts at line 45, `driverState()` is present near line 196, health counters are at lines 229-251, and register helpers are at lines 592-624.
- Dirty/cache divergence is explicit: `SettingsSnapshot::hardwareStateDirty` at `include\PCA9555\PCA9555.h:55-56`, accessors at lines 262-267, and implementation at `src\PCA9555.cpp:1702-1714`.
- Recovery and direct register access reconcile cached runtime state; see `src\PCA9555.cpp:382`, `:444`, and register/cache code around `src\PCA9555.cpp:704-748`.
- HIL runner exists as `tools\run_i2c_hil.py`; contract checker exists as `tools\check_hil_contract.py`.
- Native tests passed 144 tests.

## Best Sources To Adapt

- Keep PCA9555 as a source pattern for GPIO-expander dirty/cache divergence.
- Add HIL parser tests by adapting BME280 `tools\test_run_i2c_hil_parser.py` or SSD1315 `tools\test_hil_runner_parser.py`.
- For recovery backoff/reset, compare TCA9548A only if PCA9555 already exposes such policy; do not add reset behavior without a concrete reset owner.

## Implementation Tasks

1. Preserve `hardwareStateDirty()` naming; it is more accurate for GPIO output/config cache than `hardwareConfigDirty()`.
2. Ensure `SettingsSnapshot` exposes `hardwareStateDirty` and `hardwareStateDirtyError` whenever raw/diagnostic writes or ambiguous state-changing failures can diverge from cache.
3. Add host-side parser/classifier tests for `tools\run_i2c_hil.py`. Cover safe command list, common minimum `version`/`scan`/`probe`/`settings`/`health` commands, failure-token classification, address scan not being identity proof, and destructive command gating.
4. Ensure `getSettings()` stays cache-only and includes dirty-state root status.
5. Audit every wait/poll path for finite timeout bounds and visible status returns. Normal GPIO/register APIs must not hide retries; recovery remains explicit and application-scheduled.
6. Review README/Doxygen for raw register writes. They must say failed or partial writes set `hardwareStateDirty()` and preserve the original transport error.
7. Keep public register helpers because PCA9555 has a small, useful register map, but do not encourage applications to parse raw state instead of using typed GPIO APIs.

## API Changes Required

- None expected.

## Simplifications Before Adding Code

- Do not add another dirty-state alias unless a real cross-library caller requires it.

## Tests To Add Or Update

- Host HIL parser tests.
- Native tests only if dirty-state docs expose a mismatch.
- Maintain existing tests for `end()` safe input state and offline no-bus behavior.

## Commands To Run

- `pio test -e native`
- `pio run -e esp32s3dev`
- `python tools\check_hil_contract.py`
- If parser tests are added: `python tools\<new_hil_parser_test>.py`
- Live HIL only with a serial target and explicit operator review for output-changing commands.

## Constraints And Non-Goals

- Do not add internal thread safety or bus ownership. Applications serialize access.
- Do not reset shared I2C bus from this driver.
- Do not add hidden retries inside normal operations; recovery must remain explicit and application-scheduled.
- Preserve distinct timeout, address NACK/device-not-found, data NACK, bus, config-register mismatch, and dirty-state statuses. Do not collapse them into generic `I2C_ERROR` or use `DEVICE_NOT_FOUND` for timeout/data/bus failures.

## Risks And Open Questions

- Open: whether PCA9555 HIL should include external-loopback verification or remain serial/driver evidence only.

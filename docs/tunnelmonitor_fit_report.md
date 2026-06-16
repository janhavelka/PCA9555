# PCA9555 TunnelMonitor Fit Report

## Audit Result

- PCA9555 keeps the callback-only core. The library still does not own `Wire`, pins, or bus timeout policy.
- Driver instances are now explicitly non-copyable and non-movable.
- `OFFLINE` is now a no-I/O gate for normal public operations. `recover()` is the controlled path that may transact while offline.
- Dirty-state diagnostics are exposed for output, polarity, and configuration register pairs. Failed or uncertain writes mark the relevant pair dirty; full-pair success or a dirty no-op replay clears it.
- Input reads with `Config::applyInterruptErrata = true` are classified as compound synchronous helpers: input register-pair read, then pointer-park write. Chunked job sequencing remains the split path for one-transfer-per-poll owners.
- `applyInterruptErrataWorkaround()` is the locked public pointer-park helper. `applyInterruptErrataWorkaroundUnlocked()` is explicit for callers that already own the bus.
- `configureOutputs(mask, value)` remains a compound helper: preload output latch, then change direction. The chunked job API owns staged sequencing and instruction budgeting.

## TunnelMonitor Adapter Subset

Prefer wrapping only these stable operations:

- Input/output masks: `setOutputBits()`, `clearOutputBits()`, `toggleOutputBits()`, `configureInputBits()`, `configureOutputBits()`, `configureOutputs()`, `setInvertBits()`, `clearInvertBits()`.
- Full-port operations: `readInputs()`, `writeOutputs()`, `setConfiguration()`, `getConfiguration()`, `setPolarity()`, `getPolarity()`.
- Diagnostics and recovery: `state()`, `isOnline()`, `lastError()`, `hasDirtyState()`, `getSettings()`, `recover()`.

Leave direct register access, single-pin helpers, and example CLI helpers out of the production adapter unless a concrete diagnostic workflow needs them.

## Patterns To Port To Sibling Libraries

- Callback-only transport with driver-owned error mapping to `Status`.
- Layered raw and tracked transport wrappers, with health updates only in tracked wrappers.
- Explicit `OFFLINE` blocking and manual recovery policy.
- Dirty-state diagnostics for recoverable hardware state, including no-op replay when cached desired state still needs hardware reapply.
- Pair-bounded register helpers that match device auto-increment limits.
- Safe output enable sequencing: preload latch before changing direction.
- Clear locked/unlocked API names when optional locks are exposed.
- Example-only adapters under `examples/common/`, with no board-specific code in the library.

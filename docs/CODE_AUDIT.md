# PCA9555 Code Audit Resolution Report

Date: 2026-08-30

Branch reviewed: `main`

Synced baseline: `079b902`

## Scope and method

Every applied finding (`A1`-`A12`), documentation finding (`D1`-`D13`), open
finding (`O1`-`O10`), proposed refactor (`R1`-`R9`), and the five explicitly
considered items in the 2026-08-29 audit was checked against the synced source,
public API contracts, repository engineering rules, transport adapters, examples,
and native tests.

The review did not assume that the proposed remedy was correct. Where a smaller
or safer correction existed, that correction was used. Where the proposal
conflicted with a deliberate contract, it was rejected and the contract was
made easier to observe or test.

## Previously applied findings

The changes described by `A1`-`A12` were present in the synced baseline. They
were re-reviewed rather than accepted from the report text.

| Finding | Verdict and disposition |
| --- | --- |
| A1 | **Valid behavior defect; implementation structure corrected further.** Successful errata cleanup must not erase the health failure from the input read it follows. Failed cleanup remains health evidence. Cleanup now uses a dedicated cleanup-tracked wrapper, so `_updateHealth()` remains owned exclusively by tracked wrappers as required by the architecture. Native tests cover read-fail/park-success and park-fail paths. |
| A2 | **Valid.** Re-establishing a whole pair clears stale mismatch and uncertainty evidence. Added explicit recovery coverage. |
| A3 | **Original defect valid, original remedy not valid.** ESP-IDF transmit errors such as `ESP_ERR_INVALID_STATE`, `ESP_FAIL`, or `ESP_ERR_NOT_FOUND` do not prove which I2C phase failed, so mapping them all to address NACK can falsely report `DEVICE_NOT_FOUND`. Ambiguous callback failures now remain `BUS_ERROR`; the native scanner uses `i2c_master_probe()` where exact address presence is required. |
| A4 | **Valid and retained.** Odd-start paired reads label the second byte with the actual wrapped pair partner. An odd-register high-byte comparison test was added in the core suite. |
| A5 | **Valid and retained.** Both example restore paths attempt direction restoration even if an earlier restoration step fails. This is the safest bounded cleanup available to the example. |
| A6 | **Valid and retained.** Arduino self-test cannot leave the all-output section before its direction-restoration path. |
| A7 | **Valid and retained.** Arduino stress count is bounded to `1..10000`, matching the native example. |
| A8 | **Valid and retained.** The impossible zero-byte success branch was removed from the Wire result mapper; a successful physical result must carry the byte counts the core validates. |
| A9 | **Valid and retained.** Native `pins` obtains complete pair snapshots rather than repeating per-pin input reads and pointer cleanup. |
| A10 | **Valid and retained.** Empty `configureOutputs()` and `preloadOutputs()` requests are bus-silent but still run binding and cooperative-owner guards. Coverage was extended to all empty-mask helpers. |
| A11 | **Valid, but the proper fix is broader than the report proposed.** Observation validity is cleared centrally in `_readRegs()` only after an actual two-byte callback failure. This covers synchronous, cooperative, named, and raw pair reads while preserving observations on bus-silent rejection. |
| A12 | **Valid and retained.** Native `verbose` uses the normal tokenizer, reports when argument-free, and rejects malformed values. |

## Documentation findings

| Finding | Verdict and disposition |
| --- | --- |
| D1 | **Valid.** The cold-start README sequence now establishes a complete caller-owned image before using cached RMW helpers. No shortcut that silently adopts unexplained hardware state was added. |
| D2 | **Valid.** Missing errors and the successful `start*()` admission reply are documented. |
| D3 | **Valid.** `readObservedState()` documents its fixed three-callback cost. |
| D4 | **Mostly valid.** Durable evidence consolidation was appropriate, but “22 fault-injection cases” overstated the evidence. The table now calls them bus-silent CLI guard/rejection cases; physical and transport fault gates remain open. |
| D5 | **Valid goal, incomplete implementation.** The remaining shared-bus owner-sequence duplication was removed from `chip_notes.md`. Electrical INT wiring stays owned by chip notes; register behavior and owner-exclusive cleanup stay owned by the register reference, with cross-links. |
| D6 | **Valid.** Repository role and layout match all supported build boundaries. |
| D7 | **Valid.** Extraction-process heading residue was removed without changing cited technical content. |
| D8 | **Valid.** Durable documentation no longer uses tool-specific “AI context” framing. |
| D9 | **Valid.** Contributor gates include the native ESP-IDF build and Windows wrapper. |
| D10 | **Valid.** The ESP-IDF checker is described as a static token/contract check, not proof of runtime command parity. |
| D11 | **Valid.** Cooperative API effects and admission results are documented. |
| D12 | **Valid.** Poll, cancel, timeout, and owed-cleanup return behavior is documented. |
| D13 | **Not complete in the baseline.** The Unreleased changelog omitted several applied code and documentation changes and described the unsafe A3 mapping as a fix. It now records the full correction set and the exact evidence limits. |

## Open findings

### O1 — idempotent cached updates

**Confirmed as a medium-severity observability defect, not the reported high
severity.** The protocol shadow is caller intent, so equality does not prove the
device still holds the value. A nonzero cached update request now reasserts the
complete pair and returns the physical transport result. A zero mask remains a
bus-silent no-op after normal guards. Documentation and failure-path tests were
added.

### O2 — native ESP-IDF console setup

**Confirmed.** Default ESP-IDF VFS input is not sufficient for the interactive
blocking CLI. The example now initializes the console selected by sdkconfig:
default/custom UART, USB CDC, or USB Serial/JTAG, installs the appropriate
driver, configures line endings, makes stdin blocking, and disables stdin
buffering. Component dependencies, the static contract checker, and ESP-IDF
documentation were updated.

### O3 — detach and pending results

**Refuted.** Retaining one terminal result until it is consumed exactly once is
a binding repository contract. Silently discarding the cancellation result in
`detach()` would violate it. A replacement owner can discover the retained
request through `activeRequestId()`, consume it with `takeOperationResult()`, and
then bind. Doxygen and a native test now demonstrate that recovery path.

### O4 — apply image fails after writable verification

**The observation is correct; the proposed semantic change is not.** An apply
operation includes input read and mandatory pointer cleanup, so failure there
means the whole operation failed even when every writable pair was applied and
verified. Reporting success would hide a real requested-operation failure. The
result already distinguishes this case through `kind`, `terminalPhase`,
`completedPairs`, `mismatchPairs`, and cleanup evidence. That discriminator is
now documented and asserted for the last two apply phases.

### O5 — partial observed-state uncertainty

**Confirmed.** Uncertainty is driver-wide write-effect evidence, not merely a
property of pairs read by the current call. Partial and complete snapshots now
return the authoritative `_uncertainPairs` value unmasked.

### O6 — Arduino probe

**Confirmed.** On the pinned Arduino core, `endTransmission(false)` stages the
write for a combined transfer and does not provide a terminal address result by
itself. `probe()` now performs one raw, health-neutral command-byte write
selecting Configuration Port 0. It changes no register and can expose an address
NACK through the Arduino adapter.

### O7 — lagging nested uncertainty

**Confirmed.** `_uncertainPairs` remains authoritative, while the public nested
field is retained for source compatibility and operation snapshots. All
invalidation and establishment paths now mirror it, and `getSettings()` applies
a defensive final synchronization.

### O8 — rejected reads discard observations

**Confirmed, with broader scope than reported.** Invalidation was moved out of
individual callers and into the point after an actual pair-read callback fails.
`BUSY`, invalid parameters, and other bus-silent rejections preserve the prior
sample; terminal physical failures invalidate it consistently.

### O9 — Wire command-phase evidence

**Confirmed for Wire result 3.** A data-phase NACK proves the command byte may
have reached the device, so it is now `MAY_HAVE_COMMITTED`; required pointer
cleanup is scheduled. Result 2 remains `NOT_ATTEMPTED` because the adapter can
prove no command data byte was accepted in that case.

### O10 — regression coverage

**Confirmed.** Tests were added for full-pair recovery clearing stale mismatch
and uncertainty and for odd scalar reads comparing the high register byte.
Additional tests cover O1, O6, O8, O9, health after cleanup, near-deadline call
count, and retained-result discovery.

## Refactoring proposals

| Proposal | Decision |
| --- | --- |
| R1 | **Deferred.** Replacing explicit internal field selection with nullable pointer accessors adds indirection without a current caller needing mask-generic behavior. The existing internal callers pass one pair bit. |
| R2 | **Applied in the safer direction.** `_readPair()` and `_writePair()` now derive the pair from the register using the existing register owner, rather than introducing a second pair-to-register map. This removes contradictory arguments without duplicating register knowledge. |
| R3 | **Deferred.** The six operation phases have materially different evidence and terminal transitions. A compact table would reduce lines but obscure safety sequencing and cleanup behavior. |
| R4 | **Applied.** Shared `portRegister()`, `withPort()`, and `portValue()` helpers replace repeated byte splices and register ternaries. |
| R5 | **Applied.** One private scalar writable-port read helper owns validation, transport, and observation synchronization. |
| R6 | **Applied simply.** The fixed `nowMs` deadline decision is made once at poll entry; the loop only divides the remaining timeout budget or runs already-required post-deadline cleanup. |
| R7 | **Rejected.** The clamp is dimensionally justified by the explicit minimum of 1 ms per callback: with three whole milliseconds left, at most three callbacks can receive a nonzero timeout. A comment and boundary test now state this invariant. |
| R8 | **Applied with smaller churn.** The example-local formatting helpers retain their call sites but no longer duplicate pin arithmetic; they delegate to the public `portOf()` and `bitOf()` APIs. |
| R9 | **Rejected for this major version.** Snapshot evidence has an independent job in retained operation results and standalone observed-state reads. Removing fields is a breaking API change. Authority and synchronization are now explicit instead. |

## Considered items left unchanged

- **Shadow model:** reads remain observations and never silently become caller
  intent. Adding an adoption API without a current caller would be speculative.
- **Datasheet extraction size:** accurate page-cited reference material remains;
  only process residue was removed.
- **`Err::OFFLINE`:** retained as a documented source-compatibility value.
- **Published command constants:** `MAX_ADDRESS` and `NUM_REGISTER_PAIRS` remain
  part of the chip-description API despite having no internal caller.
- **Stress confirmation parity:** Arduino stress is read-only and is part of the
  default HIL sequence, so adding confirmation would add friction without a
  safety benefit.

## Validation

- Native PlatformIO/Unity suite: **72/72 passed**.
- Version generation consistency: passed.
- Core framework/timing guard: passed.
- Arduino CLI contract: passed.
- Native ESP-IDF example contract: passed.
- HIL contract and host parser suite: passed.
- Clean exported-package content and consumer build: passed.
- Doxygen with warnings treated as errors: passed.
- `git diff --check`: passed.
- Arduino ESP32-S2 and ESP32-S3 builds: **not reached**. The required
  repository wrapper found PlatformIO Core 6.1.19, but its existing managed
  Python 3.12 environment failed before compilation while installing `uv` into
  `penv` (exit 106). No replacement PlatformIO Core or dependency was installed.
- Native ESP-IDF target build: not available locally because `idf.py` is not
  installed; the repository's static native-IDF contract passed.

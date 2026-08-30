# PCA9555 library audit — 2026-08-29

Scope: `include/PCA9555/`, `src/PCA9555.cpp`, `examples/`, `test/`, `tools/`, `docs/`.
Baseline: `b6ed943` (v3.0.2), 65/65 native tests green, all repo contract checkers green.

Method: 11 parallel review lenses over the repository, plus independent
verification of every finding, plus my own empirical testing — I built a
faithful PCA9555 bus model and ran the real library against it
(`g++ -std=c++17 -Wall -Wextra -Wconversion`) to confirm or kill each behavioural
claim rather than reasoning from the source alone. Every "confirmed" below was
reproduced; nothing here is inferred.

This file is transient working material. Delete it once the items are closed —
it is deliberately outside `docs/` and outside the packaged file list.

---

## 1. Applied in this pass

All of these are verified: 65/65 native tests pass, `esp32s3dev` builds,
`doxygen Doxyfile` is clean, and every checker in `tools/` passes.

### Code

| # | File | Change |
| --- | --- | --- |
| A1 | [src/PCA9555.cpp](src/PCA9555.cpp) `_parkPointer()` | A successful errata pointer park no longer counts as a tracked health success. |
| A2 | [src/PCA9555.cpp](src/PCA9555.cpp) `_establishShadowPair()` | Re-establishing a pair now clears that pair's stale `_observed.mismatchPairs` bit. |
| A3 | [examples/espidf_basic/main/main.cpp](examples/espidf_basic/main/main.cpp) `mapI2c()` | `ESP_FAIL` / `ESP_ERR_NOT_FOUND` now map to `NACK_ADDRESS` instead of `BUS_ERROR`/`IO_ERROR`. |
| A4 | [examples/espidf_basic/main/main.cpp](examples/espidf_basic/main/main.cpp) `rregs` | Second byte of an odd-start pair read is now labelled with the correct register. |
| A5 | [examples/01_basic_bringup_cli/main.cpp](examples/01_basic_bringup_cli/main.cpp), [examples/espidf_basic/main/main.cpp](examples/espidf_basic/main/main.cpp) | Restore helpers always attempt the direction restore, even after an earlier restore step fails. |
| A6 | [examples/01_basic_bringup_cli/main.cpp](examples/01_basic_bringup_cli/main.cpp) `cmdSelfTest` | The all-outputs section can no longer return before restoring direction. |
| A7 | [examples/01_basic_bringup_cli/main.cpp](examples/01_basic_bringup_cli/main.cpp) `stress N` | Bounded to `1..10000`, matching the native ESP-IDF CLI. |
| A8 | [examples/common/I2cTransport.h](examples/common/I2cTransport.h) `mapWireResult()` | Removed the `case 0: return Ok(0,0)` branch the driver would reject; documented that callers own the success case. |
| A9 | [examples/espidf_basic/main/main.cpp](examples/espidf_basic/main/main.cpp) `pins` | Now built from four complete pair reads (5 transfers, one INT clear) instead of 16 × `printPinInfo` (~80 transfers, 16 INT clears). |
| A10 | [src/PCA9555.cpp](src/PCA9555.cpp) `configureOutputs()`, `preloadOutputs()` | A zero mask no longer returns `Ok` ahead of the cooperative-operation guard; it now reports `BUSY` like every sibling. |
| A11 | [src/PCA9555.cpp](src/PCA9555.cpp) cooperative `READ_INPUTS` | A failed cooperative input read now clears `PAIR_INPUTS` validity, matching the synchronous path. |
| A12 | [examples/espidf_basic/main/main.cpp](examples/espidf_basic/main/main.cpp) `verbose` | Bare `verbose` reports instead of toggling, and the argument is parsed with the file's own tokenizer instead of `strstr`. |

**A10** — both returned `Status::Ok()` before any ownership check, so
`configureOutputs(0, x)` and `preloadOutputs(0, x)` claimed success while a
compound operation owned the device. Every other public I2C method returns
`BUSY` there. `_shadowStatus(PAIR_NONE)` runs the bound and operation guards
without demanding any shadow pair.

**A11** — the synchronous `_readInputPair()` clears `PAIR_INPUTS` validity when
the read fails; the cooperative phase did not, so
`lastObservedState().valid(PAIR_INPUTS)` kept returning true over a stale sample
with a stale `observedAtMs`.

**A12** — `gVerbose = strstr(full, " 0") == nullptr && (strstr(full, " 1") !=
nullptr || !gVerbose)` made bare `verbose` toggle (the help says `verbose [0|1]`,
and the Arduino CLI reports), accepted `verbose 10` as "on", and silently ignored
garbage.

**A1 — a failed input read was reported as healthy.**
`_readInputRegisters()` runs the read, then the mandatory errata park. Both went
through the *tracked* transport wrapper, so a successful park called
`_updateHealth(Ok)`, which sets `READY` and zeroes `_consecutiveFailures`.

Measured before the fix, with the read failing and the park succeeding:

```
readInputs -> I2C_BUS   transfers=2
state=READY consecutiveFailures=0 totalFailures=1 totalSuccess=1
```

`readInputs()` is the recommended INT service path, so a firmware whose health
policy watches `state()` or `consecutiveFailures()` could never see an input
read failing. After the fix the same case reports `state=DEGRADED
consecutiveFailures=1`. A *failed* park still records a failure — that is real
transport evidence. Rule now documented in `AGENTS.md` and `README.md`.

Side effect worth knowing: `totalSuccess` no longer counts park transfers, so one
`readInputs()` increments it by 1 instead of 2. Future HIL success counts will be
lower than the 2,604 on record for the same command mix.

**A2 — a snapshot could report a pair as both shadow-valid and mismatched.**
Measured before the fix: write pair → external change → contradicting read fences
the shadow and sets `mismatchPairs` → successful full-pair rewrite re-establishes
the shadow but leaves `mismatchPairs` set. `getSettings()` then reported
`shadowValidPairs=0x02` and `observed.mismatchPairs=0x02` at the same time.

**A3 — `probe()` could never report `DEVICE_NOT_FOUND` on ESP-IDF.**
`probe()` upgrades only `Err::I2C_NACK_ADDR` to `DEVICE_NOT_FOUND`, and the
adapter produced `BUS_ERROR`/`IO_ERROR` for every failure. For a bring-up tool
whose first job is "is the chip there?", that was the one answer it could not
give.

Getting the *right* condition took disassembly, because `driver/i2c_master.h`
documents no NACK return code at all. In the shipped `libesp_driver_i2c.a`
(esp32s3, IDF v5.5.5), `s_i2c_transaction_start` ends:

```
1d4:  l32i.n a8, a7, 12      # i2c_master->status
1d9:  movi   a6, 0x103       # ESP_ERR_INVALID_STATE
1dc:  bnei   a8, 7, 1e1      # status != DONE  -> keep 0x103
1df:  movi.n a6, 0           # status == DONE  -> ESP_OK
```

So a NACK from `i2c_master_transmit` / `i2c_master_transmit_receive` surfaces as
`ESP_ERR_INVALID_STATE`; `ESP_ERR_NOT_FOUND` comes only from
`i2c_master_probe()`, and the only `ESP_FAIL` in the object is on the
asynchronous path. All three now map to `NACK_ADDRESS`. `ESP_ERR_INVALID_STATE`
also covers post-START bus faults, so the comment states this is best-effort "no
ACK observed" evidence rather than proof. `writeEffect` is unchanged, so shadow
fencing stays conservative.

**A4 — wrong register label on odd-start pair reads.** PCA9555 auto-increment
toggles inside a pair, so `rregs 3 2` returns `[0x03][0x02]`. The ESP-IDF CLI
printed `reg[0x03]` then `reg[0x04]` — a register in a different pair. The
Arduino CLI already had `autoIncrementPairRegister()`; the two examples had
drifted. Ported the helper.

**A9 — `pins` was a destructive diagnostic.** `printPinInfo()` costs five
transfers per pin (`readPin` = read + errata park, then three more), so `pins`
issued about 80 transfers and serviced the input port sixteen times — clearing
INT sixteen times while merely *inspecting* pin state, which is exactly the
state an operator is usually trying to observe. The Arduino CLI already did this
correctly with four pair reads; the examples had drifted. `pininfo <N>` still
uses the per-pin path, where five transfers is fine.

**A5/A6 — restore paths could leave the fixture driven.** `restoreOutputAndDirection()`
and `restoreWritableState()` skipped the direction restore whenever the latch
write failed, and `cmdSelfTest` returned early at the one point where all sixteen
pins are configured as driven outputs. Returning pins to inputs is the safe
state and does not depend on the latch write; the library already refuses a
direction change that would enable an output from an uncertain latch, so the
attempt fails closed rather than driving blind.

### Documentation

| # | File | Change |
| --- | --- | --- |
| D1 | [README.md](README.md) | The "Typed synchronous APIs" example was **not runnable**: from a fresh `bind()` its first line returns `SHADOW_INVALID`. Replaced with a working cold-start sequence. |
| D2 | [README.md](README.md) | Error table gained `CONFIG_REG_MISMATCH` and `UNSUPPORTED`; noted that `IN_PROGRESS` is also the success reply of `start*()`. |
| D3 | [README.md](README.md) | Design summary now admits `readObservedState()` uses three callbacks, not one. |
| D4 | [docs/hardware_validation.md](docs/hardware_validation.md) | Rewritten around an evidence table; the dated COM4 report was folded in and deleted, along with its four hard-wired references (`library.json`, `tools/check_package.py`, `docs/README.md`, and the runbook itself). |
| D5 | [docs/chip_notes.md](docs/chip_notes.md), [docs/register_reference.md](docs/register_reference.md) | The register map, pair auto-increment, interrupt behaviour and errata were stated in full in both. `register_reference.md` is now the single owner; `chip_notes.md` is board/electrical only and links to it. Output-enable ordering and the errata condition itself moved into the owner. |
| D6 | [AGENTS.md](AGENTS.md) | "Role and Target" said Arduino/PlatformIO only, contradicting a framework-neutral library with a native ESP-IDF example. Repository tree omitted `test/`, `tools/`, `scripts/`, `docs/`, `.github/`. |
| D7 | [docs/datasheet_extraction.md](docs/datasheet_extraction.md) | 13 headings prefixed `Addendum:` — residue of a second extraction pass, meaningless to a reader. Prefixes removed; no content touched. |
| D8 | [docs/README.md](docs/README.md) | Dropped the "AI-coder context" framing from a shipped package file. |
| D9 | [CONTRIBUTING.md](CONTRIBUTING.md) | Gate list omitted the ESP-IDF example build that CI runs; added, plus the Windows `scripts/pio.cmd` note. |
| D10 | [docs/espidf.md](docs/espidf.md) | Claimed the checker enforces "command-surface parity". It checks that each mandatory command *name* appears in the file. Wording corrected. |
| D11 | [include/PCA9555/PCA9555.h](include/PCA9555/PCA9555.h) | `startApplyImage()` now documents that it also reads both input ports and therefore clears both interrupt sources; all three `start*()` document the `IN_PROGRESS` admission reply. |
| D12 | [include/PCA9555/PCA9555.h](include/PCA9555/PCA9555.h) | `pollOperation()`, `cancelOperation()` and `timeoutOperation()` document their return contract — in particular that cancel/timeout return `IN_PROGRESS` while a pointer park is still owed. |
| D13 | [CHANGELOG.md](CHANGELOG.md) | `[Unreleased]` records everything applied here. |

**D1 is the one that would have cost a user real time.** Measured, verbatim from
the old README, against a fresh `bind()`:

```
preloadOutput(P03,LOW)   -> SHADOW_INVALID
setDirection(P03,OUTPUT) -> SHADOW_INVALID
writePin(P03,HIGH)       -> SHADOW_INVALID
bus transfers: 0
```

Nothing in the snippet establishes a protocol shadow, and every cached
read-modify-write helper refuses to run without one. The working cold start is
three complete pair writes:

```
preloadOutputs(0xFFFF, 0xFFFF) -> OK        # W 02 x2
setPolarity(0x0000)            -> OK        # W 04 x2
setConfiguration(0xFFFF)       -> OK        # W 06 x2
# shadow=0x0E, and the per-pin helpers work from here on
```

---

## 2. Open findings

Everything below changes behaviour a passing test asserts, or changes a published
contract, so it is your call rather than mine. Each item states what I measured,
the proposal, and the test impact. Ranked by what I would fix first.

### O1 — Cached RMW helpers silently do nothing when the value already matches the shadow

**Severity: disputed — I read it as high, independent verification read it as low
("a documentation and observability gap, not a logic error"). Both readings agree
on the mechanism; they disagree on whether "success" should imply a transfer.**

`src/PCA9555.cpp` — `writePin` (:1035), `setOutputBits` (:1090),
`clearOutputBits` (:1099), `configureInputBits` (:1200), `setPinPolarity`
(:1293), `setInvertBits` (:1311), `clearInvertBits` (:1319).

Each returns `Status::Ok()` with **zero bus traffic** when the requested value
equals the protocol shadow. Because no transfer is attempted, `_updateHealth()`
never runs: no failure counter moves, the driver never enters `DEGRADED`, and
`lastError` stays OK.

Two consequences:

1. A periodic idempotent re-assert — the standard defensive firmware pattern —
   is a silent no-op. If the expander browns out and re-PORs, or another master
   writes it, the divergence is neither corrected nor detected.
2. `writePin(P00, LOW)` returns success with the expander physically
   disconnected, so it is useless as a liveness check.

This is inconsistent with `toggleOutputBits`, `configureOutputs`,
`setConfiguration`, `preloadOutputs`, `writeOutput`, `setPortPolarity` and
`setPortConfiguration`, which always emit. It is documented nowhere, and it sits
against your own binding rules — AGENTS.md "Silent failure is unacceptable" and
"Do not hide hardware failures behind silent retries or fake success", and
README "never suppress a requested transfer".

**Proposal A (what I would do):** delete the seven value-equality short-circuits;
keep the genuine `mask == 0U` no-request guards. Cost is one 3-byte transaction
per call.

**Proposal B (the minimum, if you want to keep the elision):** document it on
each of the seven methods and in the README — "returns `Ok` without a transfer
when the requested value already equals the shadow; it is therefore not a
liveness check and will not re-assert a diverged register". Right now nothing in
the header, README, AGENTS.md, CHANGELOG or `docs/` mentions it, which is what
makes it a trap either way.

**Test impact: none.** I applied Proposal A and ran the suite: 65/65 still pass.
I reverted it only because it changes the observable I2C behaviour of seven
public methods, which is a contract decision rather than a typo fix.

### O2 — The native ESP-IDF CLI never sets up its console, so it is probably not usable interactively

**Severity: high, but unverified on hardware.**
`examples/espidf_basic/main/main.cpp` — `app_main()`.

```cpp
setvbuf(stdin, nullptr, _IONBF, 0);
...
while (true) {
  printf("> ");
  if (fgets(line, sizeof(line), stdin) != nullptr) { handleCommand(line); }
  vTaskDelay(pdMS_TO_TICKS(1));
}
```

Nothing installs the UART driver or attaches it to the VFS. Without
`uart_driver_install()` + the VFS `use_driver` call, ESP-IDF's primitive console
implementation is non-blocking: `fgets()` returns immediately, so the loop
reprints the prompt roughly a thousand times a second and typed characters can be
dropped. Line-ending translation is also unset, so a terminal that sends bare CR
never terminates a line.

This is why I rank it high: the ESP-IDF example is one half of the bench tool,
and CI only ever *builds* it. `docs/hardware_validation.md` is explicit that
native ESP-IDF has never been exercised on hardware, so nothing would have caught
this.

**Proposal (ESP-IDF v5.3+, which matches the CI's v5.4):**

```cpp
#include <driver/uart.h>
#include <driver/uart_vfs.h>
...
uart_vfs_dev_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
uart_vfs_dev_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);
ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0));
uart_vfs_dev_use_driver(UART_NUM_0);
```

A board whose console is USB Serial/JTAG (the ESP32-S3 devkit default) needs the
`usb_serial_jtag_vfs_*` equivalents instead, selected on
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`.

**I did not apply this.** I cannot build ESP-IDF locally, the correct symbols
moved between v5.2 and v5.3, and the right variant depends on the console the
board is configured for. Writing untested console setup into the one example
that has never run on hardware would be worse than leaving it flagged. This
wants one person, one board, ten minutes.

### O3 — `detach()` leaves a pending result that blocks every later `bind()`

**Severity: high.** `src/PCA9555.cpp:82-99`, `bind()` guard at :63.

Measured:

```
startApplyImage(7, ...)   -> IN_PROGRESS
detach()                  -> OK        isBound=0  state=UNINIT
bind(cfg)                 -> BUSY (BusyDetail::RESULT_PENDING)
takeOperationResult(7, r) -> OK        outcome=CANCELLED
bind(cfg)                 -> OK
```

`detach()` cancels the operation through `_finishOperation()`, which sets
`resultPending = true`. The driver is then unbound *and* unbindable. The only
escape is `takeOperationResult()` with the original request ID — so a new owner
that did not perform the original `startApplyImage()` cannot recover the object
at all.

**Proposal:** `detach()` clears the operation slot outright (`_operation =
OperationSlot{}`) after the cancellation. The retained result describes work
against a binding that no longer exists; keeping it cannot help a caller and can
only brick re-binding. Keep the existing refusal when a pointer park is still
owed — that one protects the chip, not a diagnostic record.

**Test impact:** rewrites two assertions in
`test_detach_is_passive_repeatable_and_retains_active_cancellation_result` and
`test_rebind_is_rejected_while_operation_or_result_is_pending`, plus one README
sentence. Both tests currently lock the trap in deliberately.

### O4 — Apply-image reports `FAILED` for an image that was fully applied and verified

**Severity: medium, and it is a documentation gap rather than a state-machine
bug.** `src/PCA9555.cpp:749-788`.

Measured, with only the final 1-byte errata park failing:

```
outcome=FAILED  status=I2C_BUS
completedPairs=0x0F  mismatchPairs=0x00
cleanupStatus=I2C_BUS  cleanupRequired=1
chip: out=FFFF pol=0000 cfg=FFFF   <- the image is correct on the device
```

All three writable pairs were written, read back and matched, yet a caller that
checks `outcome == SUCCEEDED` concludes the apply failed and may re-apply an
image that is already correct.

**I first proposed changing the outcome to `SUCCEEDED`. That is wrong** —
`test_apply_reports_failure_at_every_phase_without_hidden_retry`
(test/test_basic.cpp:849, registered at :2355) walks all eight phases including
`READ_INPUTS` and `POINTER_PARK` and asserts `pollOperation()` returns the
transport error for each. The behaviour is deliberate: the operation as a whole
did not complete, and the driver refuses to call a partial result a success.

**The real gap is that the discriminator is undocumented.** A caller *can* tell
"image is applied, only the errata cleanup failed" apart from "the image is not
applied", but nothing says how:

```
terminalPhase == READ_INPUTS || terminalPhase == POINTER_PARK
  && (completedPairs & PAIR_ALL_WRITABLE) == PAIR_ALL_WRITABLE
  && mismatchPairs == PAIR_NONE
  -> the three writable pairs are on the device and verified;
     cleanupStatus / cleanupRequired describe what is still owed.
```

**Proposal:** document that combination in the README cooperative-operations
section and in the `OperationResult` Doxygen, so a caller can avoid a pointless
re-apply. If you would rather change the semantics, that is a deliberate contract
change and the test above has to change with it — but I would not: "did the whole
operation finish" is a cleaner meaning for `outcome` than "did most of it".

### O5 — `readObservedState()` under-reports uncertainty on a partial failure

**Severity: medium.** `src/PCA9555.cpp:485,500`.

Measured: driver truly holds `uncertainPairs = 0x02`; a `readObservedState()`
whose first pair read fails returns a snapshot with `uncertainPairs = 0x00`,
because the code masks by `current.validPairs`, which is `0` after an early
failure. The snapshot claims certainty it does not have.

**Proposal:** report `_uncertainPairs & PAIR_ALL_WRITABLE` unmasked — the driver's
uncertainty is a property of the driver, not of what this particular call managed
to read.

**Test impact:** `test/test_basic.cpp:1025-1028` asserts the masking
(`observed.uncertainPairs & ~observed.validPairs` must be 0). That assertion
encodes the bug and would be inverted.

### O6 — `probe()` cannot report `DEVICE_NOT_FOUND` through the Arduino adapter

**Severity: medium.** `examples/common/I2cTransport.h:207-227`.

On Arduino-ESP32, `endTransmission(false)` only stages the command and returns 0
unconditionally, so the adapter's NACK branch is dead code on the real target.
The combined transfer happens inside `requestFrom()`, which returns a byte count
and no error code, so an absent device produces `IO_ERROR` — never
`I2C_NACK_ADDR`, which is the only thing `probe()` converts to
`DEVICE_NOT_FOUND`. (The equivalent ESP-IDF gap is fixed in A3; this one cannot
be fixed in the adapter without a second physical attempt, which the transport
contract forbids.)

**Proposal:** change `PCA9555::probe()` from a write-read to a single
command-byte write (`_i2cWriteRaw(&reg, 1, effect)` with `reg =
cmd::REG_CONFIG_PORT_0`). This is strictly simpler — one transfer, no RX buffer,
and the read value is discarded today anyway — and `endTransmission(true)`
returns 2 on an address NACK on every Arduino core, so the verdict becomes clean
and portable. It writes the same command byte the current probe already writes,
and 0x06 is errata-safe.

**Test impact:** `test_probe_is_explicit_one_transfer_and_health_neutral` asserts
`bus.readCalls == 1` and a `'R'` transaction; both become `'W'`.

### O7 — `ObservedState::uncertainPairs` inside a snapshot can lag the driver

**Severity: low.** `src/PCA9555.cpp:283-322` (`_syncObservedRegister`),
`getSettings()`.

`_observed.uncertainPairs` is refreshed only by a *successful* read. So after a
`MAY_HAVE_COMMITTED` write, a single `getSettings()` can report
`uncertainPairs = 0x02` at the top level and `observed.uncertainPairs = 0x00`
inside the same snapshot. Two fields with the same name disagreeing in one
struct is a diagnostic trap.

**Proposal:** stop mirroring driver uncertainty into `ObservedState` at read
time. Either set `snapshot.observed.uncertainPairs = _uncertainPairs` in
`getSettings()` so the whole snapshot is coherent, or drop the field from
`ObservedState` and let the top-level `SettingsSnapshot::uncertainPairs` own it.
The second is simpler and removes duplicated state (see R-note below).

**I originally proposed something different here and it was wrong.** I thought
`_syncObservedRegister` should clear a pair's `mismatchPairs` bit when a
single-byte read *matches*, mirroring `_recordPairObservation`. Verification
showed that would be a semantic downgrade: an unrelated later single-byte read of
the same pair would erase real divergence evidence, and `test_basic.cpp:1497`
locks in that a single-byte read fences the whole pair. The asymmetry is
deliberate — the comment at `src/PCA9555.cpp:312-314` says a single-byte
observation is not fresh evidence for the pair, and the code clears the pair from
`validPairs` accordingly. With A2 applied, the invariant
`mismatchPairs & shadowValidPairs == 0` now holds, so a snapshot can no longer
claim "shadow-valid AND mismatched". No change needed there.

### O8 — A rejected read discards a valid observation with no bus traffic

**Severity: low.** `src/PCA9555.cpp:375-378` (`_readPair`).

`_readPair` clears `_observed.validPairs` for the pair on *any* failure,
including local rejections that never touched the bus. Measured: a successful
`readOutputs()` leaves `validPairs=0x02`; a subsequent `readOutputs()` rejected
with `BUSY` because a cooperative operation owns the device leaves
`validPairs=0x00`, despite zero transfers.

**Proposal:** an observation is historical evidence with an `observedAtMs`
timestamp. A read that never happened is not evidence that the old one is wrong,
so it should not invalidate it. Move the invalidation so it only applies to an
actual transport failure.

### O9 — `wireWriteRead()` claims `NOT_ATTEMPTED` for a command-byte NACK, which skips the mandatory errata park

**Severity: medium in principle, currently unreachable on ESP32.**
`examples/common/I2cTransport.h:207-214`.

```cpp
const PCA9555::WriteEffect commandEffect =
    (result == 2U || result == 3U) ? NOT_ATTEMPTED : MAY_HAVE_COMMITTED;
```

Result 2 is an address NACK, and `mapWireResult()` already forces
`NOT_ATTEMPTED` for it — so that half of the ternary is dead and the only live
effect is: **result 3 → `NOT_ATTEMPTED`**. Result 3 means the command byte *was*
clocked out and the target NACKed it. Whether the PCA9555 latched the register
pointer before NACKing is unspecified, so this asserts proof the adapter does not
have. The library's contract is explicit — AGENTS.md: "`NOT_ATTEMPTED` is valid
only when the adapter can prove it was not [accepted]" — and it acts on it:
`src/PCA9555.cpp:434` and `:742` skip `_parkPointer()` on `NOT_ATTEMPTED`. So a
NACKed input-read command byte can leave the register pointer at `0x00` with no
errata park attempted, which is the exact hazard the library exists to prevent.

It is also internally inconsistent: `wireWrite()` maps the same result 3 to
`MAY_HAVE_COMMITTED` (:153), the `requestFrom` block twelve lines below reasons
the conservative way for the same ambiguity, and the ESP-IDF adapter is uniformly
conservative.

**Proposal:** drop the ternary and pass `MAY_HAVE_COMMITTED`. The cost of being
wrong in the safe direction is one extra 1-byte write; the cost of being wrong in
the unsafe direction is the errata.

**Test impact:** `test_wire_read_adapter_reports_command_phase_evidence`
(test/test_basic.cpp:2299) asserts `NOT_ATTEMPTED` for result 3. I applied the
fix and it fails with "Expected 1 Was 3", so the assertion encodes the current
behaviour and would need updating.

**Why I left it:** on Arduino-ESP32 `endTransmission(false)` only stages the
command and always returns 0, so this whole branch is dead on the validated
target — and that also means the test exercises a path the real hardware never
takes. The exposure is real only on a non-ESP32 Arduino core.

### O10 — Two test gaps that would let a real regression through

**Severity: medium** (the code is correct today; the safety net is missing).

1. **Uncertainty recovery is untested.** No test rewrites a complete pair to
   clear `uncertainPairs` — the only synchronous escape from `STATE_UNCERTAIN`.
   A regression at the `_establishShadowPair` call site in `_writeRegs`
   (`src/PCA9555.cpp:366-369`) that left a pair permanently uncertain, escapable
   only via `startVerifyImage()`, passes all 65 tests. Given how much of the API
   is fenced behind that flag, it deserves a test: ambiguous write → assert
   `STATE_UNCERTAIN` → full-pair rewrite → assert the cached helper works.
2. **Odd-register single-byte mismatch is untested.** No test performs a
   successful *odd*-register scalar read of a writable register whose value
   differs from the shadow's **high** byte, so the `(reg & 0x01U)` branch of
   `_syncObservedRegister`'s comparison never runs with a contradicting value.
   The existing odd-register scalar read (`test_basic.cpp:2022`) asserts a
   matching value. A swapped high/low byte selection there would go unnoticed.

Both are additive tests against unchanged behaviour.

---

## 3. Considered and not changed

- **The shadow model itself.** After a fresh `bind()` every cached RMW helper
  returns `SHADOW_INVALID`, and after a POR the pairs stay fenced until the
  caller states an intent. I checked whether reads should establish the shadow.
  They should not: `test_ordinary_pair_reads_fence_only_the_externally_changed_shadow_pair`,
  `test_named_and_raw_reads_fence_the_whole_pair_on_any_observed_mismatch` and
  `test_successful_read_after_por_updates_observed_not_write_shadow` lock the
  fencing in deliberately, and the reasoning is sound: silently adopting a
  register value nobody asked for is how an RMW clobbers bits after an
  unexplained change. The real gap was that the README's own example did not show
  a working bootstrap — fixed in D1. If you later want a cheaper escape than
  `startApplyImage()`, the minimal addition is an explicit zero-I2C
  `adoptObservedState(uint8_t pairs)`, which keeps "the caller states the
  intent" while costing no transfers; I did not add it because AGENTS.md asks for
  a concrete current caller first.
- **`docs/datasheet_extraction.md` bulk.** Roughly 300 of its 1129 lines have no
  bearing on a software driver (packages and part markings, latch-up/ESD ratings,
  absolute maximum ratings, thermal information, parameter measurement
  conditions, pin capacitance, QFN pin mapping, layout guidance, typical
  application circuit, the page-level citation index). It is accurate, cited, and
  explicitly scoped in `docs/README.md`, so I trimmed only the extraction-process
  residue (D7) and left the content. Say the word and I will cut the sections
  above.
- **`Err::OFFLINE`.** Never produced anywhere in `src/`. That is intentional and
  documented as a reserved v2 source-compatibility value; removing it would be a
  breaking change for no gain.
- **`cmd::MAX_ADDRESS` and `cmd::NUM_REGISTER_PAIRS`.** Unused by any code, but
  they are part of a published register-description header where documenting the
  chip is the point. Removing them is a breaking change with no benefit.
- **`stress N` confirmation parity.** The native CLI requires `confirm`; the
  Arduino CLI does not. I bounded the count (A7) but left confirmation alone —
  the Arduino command is read-only and non-blocking, and `stress 10` without
  `confirm` is in the HIL default command sequence.

---

## 4. Refactoring plan

The core is correct but repetitive. None of this changes behaviour; all of it is
mechanical and testable against the existing suite. Ordered by readability gained
per unit of risk.

### R1 — One pair accessor instead of 23 hand-written `if (pair == ...)` lines

`src/PCA9555.cpp` has six separate chains that map a `StatePair` bit to a
`uint16_t` field: `_establishShadowPair` (:245-247), `_shadowValue` (:253-255),
`_recordPairObservation` (:261-264), `_syncObservedRegister` (:299-302 and
:312-315), `_compareObserved` (:559-561).

They all compare `pair` with `==` against a single-bit constant, so passing
`PAIR_ALL_WRITABLE` silently does nothing or returns 0 — a footgun sitting next
to `_shadowStatus()` and `valid()`, which *do* take masks.

```cpp
// Return the field of `image` selected by a single pair bit, or nullptr.
static uint16_t* pairField(RegisterImage& image, uint8_t pair);
static const uint16_t* pairField(const RegisterImage& image, uint8_t pair);
```

With `ObservedState::inputs` handled by the one caller that needs it, this
replaces all six chains and makes the single-bit precondition explicit and
assertable.

### R2 — Derive the register from the pair

`_readPair()` and `_writePair()` take *both* a start register and a pair bit that
must agree, at 25 call sites. One `constexpr uint8_t pairBaseRegister(uint8_t
pair)` removes the second argument and the possibility of them disagreeing.

### R3 — Collapse the six near-identical `_executeOperationTransfer` cases

The three `APPLY_*` cases differ only in register, expected field and next phase;
the three `VERIFY_*` cases differ only in register, pair and next phase. A small
static table of `{phase, pair, nextPhase}` plus R1/R2 turns a 157-line switch
into roughly 40 lines with the same behaviour.

### R4 — One port helper instead of six inline byte splices

`(intended & 0xFF00U) | value` and its high-byte twin appear at :983, :985,
:1139, :1141, :1243, :1245, and `port == Port::PORT_0 ? REG_x_0 : REG_x_1`
appears eight times. `PortData` already owns `combined()`/`fromCombined()`; add
`withPort(uint16_t combined, Port port, uint8_t value)` and
`constexpr uint8_t portRegister(uint8_t baseReg, Port port)`.

### R5 — One scalar-register read helper

`readOutput` (:1001), `getPortConfiguration` (:1157) and `getPortPolarity`
(:1261) are three copies of the same eight-line body differing only in the
register pair. One private `_readPortByte(Port, uint8_t baseReg, uint8_t& value)`
covers all three.

### R6 — De-duplicate `pollOperation`'s deadline block

The deadline/cleanup-owed block at :797-817 is repeated almost verbatim at
:845-863, and the copies have already drifted (the loop copy also sets
`_callbackTimeoutMs`). Hoist it into one lambda or private helper.

### R7 — Drop the millisecond-as-count clamp

`pollOperation` clamps `callLimit` (a number of callbacks) with
`timeoutAllowance` (milliseconds remaining) at :838-841. It happens to be
conservative, but comparing a duration against a count is a unit confusion that
makes the budget logic hard to read. The per-callback fair-share computation
below it already bounds the poll's wall time.

Measured for context: with `Config::i2cTimeoutMs = 50` and a 40 ms
whole-operation timeout, all eight apply-image callbacks were handed 5 ms; with a
3 ms whole-operation timeout, 1 ms. That is the documented fair-share behaviour,
not a bug, but it is worth knowing that `Config::i2cTimeoutMs` is an upper bound
rather than the value used.

### R8 — Example glue duplicates library helpers

Both examples define `physicalPortForPin()` and `physicalBitForPin()`, which are
`PCA9555::portOf()` and `PCA9555::bitOf()`. 15+ call sites in the Arduino example
alone. Deleting them shows readers the helpers the library actually ships.

### R9 — Duplicated evidence fields between `ObservedState` and the driver

`ObservedState` carries `mismatchPairs` and `uncertainPairs`, and so does
`SettingsSnapshot` / the driver itself. The `ObservedState` copies are snapshots
taken at read time and go stale (that is O7). Since `getSettings()` already
returns both, the `ObservedState` copies have no independent job. Dropping them
removes a whole class of "which one is authoritative?" questions.

---

## 5. How this was checked

- 11 review lenses over the tree produced 97 raw findings, 96 after dedupe.
- Each was then verified by an independent agent instructed to refute first,
  check the finding against the documented contract, construct a concrete
  reproduction, and look for a test asserting the opposite. **16 survived; 80
  were refuted** — mostly stale line numbers, behaviour that turned out to be the
  documented contract, or items already fixed earlier in this same pass.
- Verification twice corrected *me*: it showed my first ESP-IDF NACK mapping was
  dead code (by disassembling `libesp_driver_i2c.a`), and that my proposed fixes
  for O4 and O7 were wrong. Both are written up above as they actually stand.
- Every applied change was re-checked against: 65/65 native tests,
  `pio run -e esp32s3dev`, `doxygen Doxyfile`, and all six checkers in `tools/`.
  Core compiles clean under `-Wall -Wextra -Wconversion -Wsign-conversion
  -Wshadow`. The ESP-IDF example cannot be built here, so its edits were
  type-checked function-by-function against the real headers instead; CI builds
  it for esp32s2 and esp32s3.

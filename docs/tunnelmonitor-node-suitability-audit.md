# PCA9555 suitability audit for TunnelMonitor-node

- Audit date: 2026-07-19
- Baseline PCA9555 revision: `46b441e44e6d43ebfaccb255a3b582122463fe94` (`v2.0.0`)
- Audited v3 implementation: `47b6f90d181c32b9ec6fe09093f7d735dfff34bf`
- TunnelMonitor-node revision: `b708f511964db6c51e949e99c67820476f00f9c7`
- Release target: PCA9555 `3.0.0`

The original audit was read-only across both repositories. TunnelMonitor-node
was not changed. This file now records the current disposition only. The
superseded line-by-line v2 analysis and implementation plan remain available in
Git at commit `4f1b03c`; they were removed from the live document so completed
refactor instructions are not mistaken for current work.

## Conclusion

The v3 PCA9555 contract resolves the library blockers found in v2. The library
is now a passive chip driver that can sit behind one TunnelMonitor-owned I2C
adapter:

- binding and detaching perform no I2C;
- each transport callback reports one terminal attempt and conservative effect
  evidence;
- the caller owns serialization, scheduling, retry, health policy, and bus
  recovery;
- apply, verify, and input service are fixed-capacity cooperative operations;
- input-read pointer cleanup is explicit, bounded, and non-interleaved;
- caller intent, protocol shadow, observations, mismatches, and uncertain state
  are separate;
- cached read-modify-write helpers reject invalid or uncertain shadow state;
- health is observational and never blocks an owner-requested transfer.

The library is suitable in principle for static digital inputs, relays,
enables, and on/off mux control. It is not a PWM device, cannot guarantee an
electrical safe state during reset, cannot make cross-port changes atomic, and
does not define TunnelMonitor product channels or policy.

No library-contract blocker remains from this audit. Adoption still requires
the external product, integration, and hardware evidence listed below. This is
not a hardware or field-readiness claim.

## Disposition matrix

Status meanings:

- **Resolved in v3**: the PCA9555 repository provides the required contract.
- **Open externally**: the fact or implementation belongs to TunnelMonitor,
  product hardware, release work, or target validation.

| ID | Finding | Current disposition | Evidence | Status |
| --- | --- | --- | --- | --- |
| TM-1 | No target device or channel map | Address, pin map, direction, safe levels, polarity, unused pins, INT, and required/optional policy remain product facts. | `Config::i2cAddress`; caller-owned `RegisterImage` | **Open externally** |
| TM-2 | PCA9555 cannot replace PWM outputs | Keep PWM on ESP32 LEDC or a dedicated PWM device. No software-PWM helper was added. | Public API contains static GPIO/register operations only. | **Open externally** |
| TM-3 | Software cannot guarantee safe OFF during reset | Hardware must provide safe bias. Reset and brownout behavior requires target evidence. | Passive `bind()`; `docs/hardware_validation.md` | **Open externally** |
| TM-4 | Critical cross-port changes are not atomic | Use same-port mapping or application/hardware break-before-make. | Paired transfers guarantee register-pair ordering, not atomic pin changes. | **Open externally** |
| TM-5 | TunnelMonitor output ownership cannot call I2C directly | Add one owner-private adapter under TunnelMonitor's I2C owner. | Exact-ID `start*` and `pollOperation()` API | **Open externally** |
| TM-6 | TunnelMonitor lacks PCA9555 contract slots | Add project device, operation, result, health, and channel descriptors in the product repository. | No TunnelMonitor source was changed here. | **Open externally** |
| LIB-1 | Dirty state did not fence cached writes | Writable shadow validity and uncertainty are tracked per pair. Unsafe cached writes fail without I2C. | `shadowValidPairs()`, `uncertainPairs()`, native shadow-fence tests | **Resolved in v3** |
| LIB-2 | Readback overwrote recovery intent | `ObservedState` is evidence only. Recovery images remain caller-owned. | `RegisterImage`, `ObservedState`, verify/reconciliation tests | **Resolved in v3** |
| LIB-3 | Failed repeated begin could abandon a live instance | Passive replacement validates first and preserves a valid binding on failure. | `bind()`/`begin()` lifecycle tests | **Resolved in v3** |
| LIB-4 | `end()` applied policy and could not report failure | `detach()`/`end()` return `Status`, perform zero I2C, and do not select hardware policy. | Passive detach tests | **Resolved in v3** |
| LIB-5 | Startup rejected retained non-default state | Presence and POR-default checks are separate explicit diagnostics. | `probe()`, `checkPorDefaults()` | **Resolved in v3** |
| LIB-6 | Raw direction writes bypassed safe preload | Raw Configuration writes are rejected. Typed direction APIs preserve latch-before-direction ordering. | Direct-register and safe-direction tests | **Resolved in v3** |
| LIB-7 | No state-integrity check after independent PCA reset | Caller-owned apply and verify operations report exact mismatch evidence. | `startApplyImage()`, `startVerifyImage()`, `mismatchPairs` | **Resolved in v3** |
| LIB-8 | Local offline policy conflicted with the bus owner | Offline admission and threshold policy were removed. Health is observational. | Three-state `DriverState`; no-gating tests | **Resolved in v3** |
| ARCH-1 | Lifecycle and recovery did too much synchronous work | Lifecycle is zero-I2C. Compound work has fixed phases, deadlines, IDs, and transaction budgets. | Maximums: 8 apply, 3 verify, 2 input callbacks | **Resolved in v3** |
| ARCH-2 | Recovery policy had two owners | Retry, bus recovery, absent-device policy, and recovery-image selection belong to the caller. | No library recovery API or retry loop | **Resolved in v3** |
| ARCH-3 | Input errata cleanup had to remain mandatory | Accepted or uncertain failed input commands still owe one pointer park. Proven not-attempted commands do not. | Cleanup result fields and fault tests | **Resolved in v3** |
| ARCH-4 | Transport and operation state were mixed | A callback returns one terminal `TransportResult`; operation progress is exposed separately. | `TransportCode`, byte counts, `WriteEffect`, operation enums | **Resolved in v3** |
| ARCH-5 | Ambiguous writes lacked reconciliation | Possibly committed writes terminate with uncertainty and never advance or retry silently. | Ambiguous-write and reconciliation tests | **Resolved in v3** |
| PKG-1 | Package and external-consumer validation were weak | The package uses an export allowlist and is unpacked and compiled as an external consumer. | `tools/check_package.py`, CI package gate | **Resolved in v3** |
| DOC-1 | Version, support, pinning, and contribution text were stale | Metadata is generated from `library.json`; README, security, contribution, and release guidance agree. | Version check and strict Doxygen gate | **Resolved in v3** |
| VAL-1 | Target hardware evidence was incomplete | Host and compile gates cannot replace real board, INT, brownout, or shared-bus evidence. | Release checklist and hardware matrix | **Open externally** |

## API additions retained after review

The refactor kept only types and helpers with current safety or caller value:

- `Pin`, `Port`, `Level`, `Direction`, and `PinMask` remove ambiguous numeric
  mapping;
- `pinIndex()`, `pinMask()`, `portOf()`, `bitOf()`, `isOutput()`, and
  `levelFor()` centralize repeated mapping rules;
- `errorName()` provides stable diagnostic names without core logging;
- `TransportResult`, `TransportCode`, and `WriteEffect` preserve terminal
  transport facts without leaking framework errors;
- `RegisterImage` and `ObservedState` separate intent from observation;
- operation enums and `OperationResult` expose bounded progress and terminal
  evidence;
- `StatePair` bits represent valid, completed, mismatched, and uncertain pairs
  without dynamic containers.

No generic queue, task, registry, plugin interface, rare-operation framework,
NVM API, calibration API, production test double, or software PWM was added.

## Remaining admission work

Complete these tasks outside this library before TunnelMonitor adoption:

1. Freeze the schematic facts and per-product channel table.
2. Keep PWM channels on ESP32 LEDC or a dedicated PWM device.
3. Prove safe electrical states across cold boot, MCU-only reset,
   PCA9555-only brownout, watchdog reset, and disconnected I2C.
4. Keep hazardous selectors on one port or define and test
   break-before-make with a hardware gate.
5. Add append-only TunnelMonitor contracts and one owner-private adapter.
6. Define the safe `RegisterImage`, deadlines, retry eligibility,
   reconciliation policy, health projection, and absent-device behavior.
7. Pin the reviewed v3 release or immutable commit.
8. Complete the target HIL matrix and shared-bus soak.

## Validation evidence

The audited implementation passed:

- generated version metadata checks;
- core framework/timing, Arduino CLI, ESP-IDF static, HIL contract, and HIL
  parser checks;
- 64 of 64 native fault and contract tests;
- packaged-content and clean external-consumer compilation;
- Arduino ESP32-S2 and ESP32-S3 compilation;
- strict Doxygen generation with zero warnings;
- an independent follow-up contract review.

Evidence limits remain explicit:

- local PlatformIO used Core 6.1.18 while the repository and CI pin 6.1.19;
- `idf.py` was unavailable locally; CI jobs are configured, but this audit did
  not retrieve a current CI result;
- no physical HIL, logic-analyzer capture, brownout test, or shared-bus soak was
  performed.

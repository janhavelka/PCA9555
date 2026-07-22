# TunnelMonitor-node Adoption Gaps

Audit date: 2026-07-19. PCA9555 v3 resolved the library-contract blockers found
against the v2 baseline. The completed disposition and refactor history remain
available in Git; this file keeps only work still required outside this library.

No library-contract blocker remains, but this is not a hardware or field-
readiness claim. Before TunnelMonitor adoption:

1. Freeze the schematic facts and channel table: PCA9555 address, pin mapping,
   directions, safe levels, polarity, unused pins, INT wiring, and whether the
   device is required or optional.
2. Keep PWM channels on ESP32 LEDC or a dedicated PWM device; PCA9555 provides
   static digital I/O only.
3. Prove safe electrical states during cold boot, MCU-only reset,
   PCA9555-only brownout, watchdog reset, disconnected I2C, and reconnect.
4. Keep hazardous selectors on one port or define and test break-before-make
   behavior with a hardware safety gate; cross-port pin changes are not atomic.
5. Add append-only TunnelMonitor device, operation, result, health, and channel
   contracts plus one owner-private PCA9555 transport adapter.
6. Define the product's safe `RegisterImage`, operation deadlines, retry
   eligibility, reconciliation policy, health projection, and absent-device
   behavior.
7. Pin PCA9555 v3.0.0 or a later reviewed immutable revision.
8. Complete the target HIL matrix and shared-bus soak in
   [hardware validation](hardware_validation.md).

TunnelMonitor-node itself was not changed by this repository audit.

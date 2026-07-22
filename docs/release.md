# Release Status

Version 3.0.0 is tagged. Its source, package, build, native-test, and Doxygen
gates were completed before release; those results belong in CI and Git history,
not in a live to-do list.

The release is still a pre-production candidate because real-target validation
is incomplete. The only open release work is to:

1. Complete and review every applicable gate in
   [hardware validation](hardware_validation.md).
2. Use a stable command channel that does not reset the target during the
   continuous shared-bus soak.
3. Record Arduino ESP32-S2, Arduino ESP32-S3, and native ESP-IDF hardware
   evidence, or approve an explicit product exclusion.
4. Prove reset, brownout, reconnect, INT, pointer-park, output-preload, and fault
   behavior on the final hardware and bus owner.

Until then, describe the library as production-oriented or designed for
deterministic owner integration, not as field-proven or fully hardware
validated.

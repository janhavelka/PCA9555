# Contributing

Keep changes small, explicit, and testable. The core is a passive,
framework-neutral device driver; board policy and bus ownership stay in the
application or examples.

## Before opening a pull request

1. Create a focused branch.
2. Follow the existing formatting and naming in the touched file.
3. Add or update native tests for every behavior change and failure path.
4. Update public API comments, README, and CHANGELOG when contracts change.
5. Run the relevant validation gates:

```text
python scripts/generate_version.py check
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python tools/check_hil_contract.py
python tools/test_run_i2c_hil_parser.py
python -m platformio test -e native
python -m platformio run -e esp32s2dev
python -m platformio run -e esp32s3dev
python tools/check_package.py
doxygen Doxyfile
```

`doxygen Doxyfile` is strict: undocumented public symbols and documentation
errors fail the command. Generated output under `docs/doxygen/` is local and
ignored; do not add it to a commit or package.

6. Review `git status --short`, `git diff --check`, and the final diff for
   generated artifacts, broken relative documentation links, or unrelated user
   changes.

## Engineering rules

- Do not include Arduino, Wire, ESP-IDF, FreeRTOS, or board headers in public
  headers or `src/`.
- Do not let the driver own or reconfigure I2C.
- Transport callbacks represent exactly one terminal attempt. Do not hide
  retries or bus recovery in a callback or in the driver.
- Keep bind/detach passive. Presence, diagnostics, apply, and verify are
  explicit operations.
- Keep compound work fixed-capacity, deadline-bounded, cooperatively polled, and
  observable at every terminal path.
- Preserve latch-before-direction ordering and the input-read pointer-park
  requirement.
- Report uncertain write effects; do not guess or silently claim success.
- Use static error strings, no exceptions, and no steady-state heap allocation.
- Add a new abstraction only for a concrete current caller or chip requirement.
- Do not add application queues, policy registries, logging, software PWM, or a
  speculative rare-operation framework to the chip driver.

## Pull request scope

Good changes include focused bug fixes, safety tests, clear documentation,
portability improvements, and examples of common integrations. Discuss a
breaking public API change before implementation. A breaking release needs a
major version, migration notes, and synchronized generated metadata.

Hardware claims need attached real-target evidence. Compile and host-test
results alone do not establish electrical, interrupt, brownout, or field
behavior.

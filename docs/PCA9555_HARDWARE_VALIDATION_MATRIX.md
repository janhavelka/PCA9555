# PCA9555 Hardware Validation Matrix

No Prompt 07 hardware validation was run. Every test below is intentionally
marked `NOT RUN` until a real bench run records board, wiring, command logs, and
evidence.

## Bench Setup Template

- MCU target: ESP32-S2 and/or ESP32-S3, exact board revision: TBD
- Firmware path: `examples/01_basic_bringup_cli` for Arduino/PlatformIO and
  `examples/esp_idf/basic` or equivalent app for pure ESP-IDF
- Host-side Arduino CLI HIL runner: `python tools/run_i2c_hil.py --port <PORT> --baud 115200 --address 0x20`
- Dry-run planning only: `python tools/run_i2c_hil.py --dry-run`
- Default Arduino CLI wiring: SDA GPIO8, SCL GPIO9, 400 kHz, address `0x20`,
  serial `115200`
- PCA9555 VCC: TBD
- PCA9555 address pins: A0/A1/A2 tied hard high or low, no floating pins
- INT: pulled up and captured by MCU or logic analyzer
- Loads: current-limited safe loads only, target <= 2 mA per pin for validation
- Evidence: serial logs, logic analyzer captures, photos, and wiring notes

## Safety Notes

- Do not externally drive a PCA9555 pin while it is configured as an output.
- Use current-limited loads; do not use absolute maximum current ratings as test
  targets.
- Tie A0/A1/A2, INT, and unused inputs deliberately.
- For brownout tests, switch only the intended rail and keep signal injection
  through safe impedance.
- `probe()` proves address response only. It does not prove chip identity.
- Automated HIL summaries under `hil_logs/` are evidence containers, not
  production validation claims by themselves. Manual and visual rows remain
  `OPERATOR_CHECK_REQUIRED` until evidence is attached.
- `recover()` reapplies cached desired state. It cannot force a true PCA9555 POR.

## Matrix

| Test | Setup | Command/procedure | Expected result | Result | Evidence |
| --- | --- | --- | --- | --- | --- |
| 1. Wired address scan/probe for all intended addresses | PCA9555 A0/A1/A2 strapped for each intended `0x20`-`0x27` address; matching firmware `Config::i2cAddress` | Run CLI `scan`, `probe`, and `drv` for each wired address | Only intended addresses respond; `probe` returns OK; `probe` does not update health counters | NOT RUN | Pending serial logs and address table |
| 2. POR defaults | Full PCA9555 power cycle; inputs documented as high/open/low by fixture | Run `dump`, `outputs`, `config`, `polarity`, and `inputs` before changing state | Config `0xFFFF`; output latch `0xFFFF`; polarity `0x0000`; input values match physical levels and are not assumed fixed | NOT RUN | Pending serial log, fixture state, and power-cycle note |
| 3. Input reads on all 16 pins | All pins configured input; switch/jumper fixture can pull each pin low through resistor | Run `dirin 0xFFFF`; for each pin, read released/high and pulled-low state with `read pin N` and `pins` | Each linear pin `0-15` maps to the correct `P00-P07` or `P10-P17`; high reads `1`, low reads `0` before polarity inversion | NOT RUN | Pending per-pin checklist and serial log |
| 4. Output writes on all 16 pins through safe preload | All pins connected to safe current-limited loads; no external driver conflicts | Use safe-output APIs or CLI flow equivalent to preload before output enable, then run `alllow`, `allhigh`, `pattern 0xAAAA`, `pattern 0x5555`, `walk 50`, and `sweep 50` | Physical outputs match latch patterns with no excessive current, resets, or I2C errors | NOT RUN | Pending photos/video and serial log |
| 5. Bulk output mask write with unrelated pins unchanged | Known output latch baseline and observable safe loads | Set a baseline, apply masked output changes, then read `outputs` and observe unrelated pins | Selected bits change; unrelated latch bits and physical outputs remain unchanged | NOT RUN | Pending serial log and pin-state record |
| 6. Latch preload before direction change | Logic analyzer on SDA/SCL and selected pins such as P00/P10; pins initially input | Capture transition from input to output low and high using `configureOutputs()` or `preloadOutput()` plus `setDirection()` | I2C decode shows Output Port write before Configuration write; selected pin does not glitch opposite the requested preload | NOT RUN | Pending logic analyzer capture |
| 7. Output-to-input false-interrupt observation/handling | INT pulled up and captured; output pin can be safely reconfigured to input with known external level | Drive known output, change to input, observe INT, then read containing input port | Any false interrupt is observed/handled as documented; input read rebaselines the port | NOT RUN | Pending INT waveform and serial log |
| 8. Polarity inversion | At least one input pin can be held high/low; output latch observed separately | Toggle polarity for a held-low and held-high pin, then read input and output latch | Input sense inverts when polarity bit is set; output latch readback is unchanged | NOT RUN | Pending serial log and fixture note |
| 9. INT assert and clear for port 0 | INT pulled up; P00 or another Port 0 input switchable | Baseline inputs, toggle Port 0 input, read Port 0 only | INT asserts low on Port 0 input change and clears after reading Port 0 | NOT RUN | Pending logic analyzer and serial log |
| 10. INT assert and clear for port 1 | INT pulled up; P10 or another Port 1 input switchable | Baseline inputs, toggle Port 1 input, read Port 1 only | INT asserts low on Port 1 input change and clears after reading Port 1 | NOT RUN | Pending logic analyzer and serial log |
| 11. INT assert and clear for both ports | INT pulled up; one input on each port switchable | Toggle one input on each port, then call `readInputsAndClearInterrupt()` or `clearInterrupts()` | INT asserts for both changes and clears after both input ports are read | NOT RUN | Pending logic analyzer and serial log |
| 12. Errata workaround pointer-park on shared bus | PCA9555 plus a second readable I2C slave; logic analyzer decodes SDA/SCL | Run `inputs`, `rin 0`, and `rin 1`; then perform other-slave reads through serialized bus manager | After each input read, PCA9555 command pointer is parked with command `0x02`; no other transaction interleaves between input read and pointer park | NOT RUN | Pending I2C decode |
| 13. NACK / wrong address behavior | Firmware can select an unused address, or PCA9555 can be safely disconnected | Configure wrong address or disconnect device; run `probe`, `begin`, `drv`, and selected read/write calls | Address errors map to documented `Status` codes; health counters and offline behavior match contract | NOT RUN | Pending serial log |
| 14. Unplug/replug recovery | PCA9555 can be safely disconnected and reconnected without damaging bus | Start READY, unplug or disable PCA9555, run enough operations to reach OFFLINE, reconnect, run `recover`, then `drv` and `inputs` | Failures move to DEGRADED/OFFLINE; normal I2C is blocked while offline; `recover()` returns READY after reconnect | NOT RUN | Pending serial log and reconnection notes |
| 15. Brownout/power-cycle recovery | PCA9555 VCC independently switchable while MCU remains powered; safe signal impedance | Drive known state, power-cycle PCA9555 only, read registers, run `recover`, read registers again | Hardware may show POR defaults before recovery; `recover()` reapplies cached desired output/polarity/config if bus is healthy | NOT RUN | Pending serial log and VCC waveform |
| 16. 100 kHz I2C operation | Validation firmware/app sets I2C bus to 100 kHz | Run `scan`, `probe`, `selftest`, `stress 1000`, and selected input/output checks | Operations complete without unexplained failures; final health state READY | NOT RUN | Pending 100 kHz command log |
| 17. 400 kHz I2C operation | Repo default wiring and bus speed or equivalent 400 kHz setup | Run `scan`, `probe`, `selftest`, `stress 1000`, and selected input/output checks | Operations complete without unexplained failures; final health state READY | NOT RUN | Pending 400 kHz command log |
| 18. Long shared-bus soak | PCA9555 plus at least one other active I2C slave; stable supply; safe loads | Run long read/write soak such as `stress 10000` and safe `stress_mix`; include other-slave traffic through bus manager | No hangs, resets, unexpected INT loss, or unexplained I2C failures; final health state READY | NOT RUN | Pending soak duration, logs, and I2C sample |
| 19. Arduino ESP32-S2 hardware run | ESP32-S2 target using `env:esp32s2dev` and CLI example | Upload, monitor, run version/help, scan/probe, read inputs, safe output toggle, and `drv` | CLI runs on ESP32-S2; PCA9555 operations match expected wiring and final health is READY | NOT RUN | Pending board ID, command log, and wiring photo |
| 20. Arduino ESP32-S3 hardware run | ESP32-S3 target using `env:esp32s3dev` and CLI example | Upload, monitor, run version/help, scan/probe, read inputs, safe output toggle, and `drv` | CLI runs on ESP32-S3; PCA9555 operations match expected wiring and final health is READY | NOT RUN | Pending board ID, command log, and wiring photo |
| 21. ESP-IDF hardware run if IDF example exists | ESP32-S2 and/or ESP32-S3 with ESP-IDF v5.4 line and `examples/esp_idf/basic` or equivalent native-IDF app | Build/flash native ESP-IDF app, run `begin`, `readInputs`, optional known-safe `configureOutputs()`, and cleanup | Native ESP-IDF transport path works without Arduino/Wire; errors map to `Status`; final cleanup succeeds | NOT RUN | Pending IDF build/flash log and serial output |

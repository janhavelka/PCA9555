# ESP-IDF Port Implementation

The driver core remains portable by requiring applications to inject transport and timing callbacks. When `Config::nowMs` is null, health timestamps are `0`; framework time sources belong in examples or application glue.

The native ESP-IDF example owns only example-local resources:

- `i2c_new_master_bus`, `i2c_master_transmit`, `i2c_master_transmit_receive`
- a persistent `i2c_master_dev_handle_t` for the active PCA9555 address
- `esp_timer_get_time()` through `Config::nowMs`
- `vTaskDelay()` for the CLI loop
- fixed command buffers for console input

The Arduino example and ESP-IDF example share a command contract, not
implementation source. The ESP-IDF implementation keeps its own fixed-buffer
parser and requires explicit `confirm` suffixes before any command that drives
outputs, changes direction, changes polarity, writes raw registers, runs output
patterns, performs recovery, or executes stress/self-test flows. Unconfirmed
guarded commands print the exact confirmed form instead of mutating hardware.

ESP-IDF hardware validation is pending. The repo-local checks prove native-IDF
boundaries and command-surface coverage only; final output-driving validation
must be repeated on target hardware.

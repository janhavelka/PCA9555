# ESP-IDF Port

The core library is framework-neutral. Public headers and `src/` do not include Arduino or ESP-IDF framework headers, and all hardware access is supplied through `Config` callbacks.

The ESP-IDF example in `examples/espidf_basic` is a native IDF application:

- entry point is `app_main()`
- I2C uses `driver/i2c_master.h`
- timestamps use an injected `Config::nowMs` callback backed by `esp_timer_get_time()`
- delays use `vTaskDelay()`
- command input uses fixed C buffers and `fgets()`
- output-driving, direction, polarity, raw write, pattern, recovery, self-test,
  sweep/walk, and stress commands require a final `confirm` token

When a guarded command is entered without `confirm`, the CLI prints the pending
change, why confirmation is required, and the exact confirmed command form. The
IDF example must not include Arduino sources or use Arduino compatibility
facades. `tools/check_idf_example_contract.py` enforces the native-IDF boundary,
command coverage, confirmation guard, and key mutating command surface.

Hardware validation status: the ESP-IDF command contract is statically checked,
but ESP-IDF hardware smoke testing and output-driving validation are still
pending until target PCA9555 hardware is available.

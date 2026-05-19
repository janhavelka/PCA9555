# ESP-IDF Port Implementation

The driver core remains portable by requiring applications to inject transport and timing callbacks. When `Config::nowMs` is null, health timestamps are `0`; framework time sources belong in examples or application glue.

The native ESP-IDF example owns only example-local resources:

- `i2c_new_master_bus`, `i2c_master_transmit`, `i2c_master_transmit_receive`
- `esp_timer_get_time()` through `Config::nowMs`
- `vTaskDelay()` for the CLI loop
- fixed command buffers for console input

The Arduino example and ESP-IDF example share a command contract, not implementation source.

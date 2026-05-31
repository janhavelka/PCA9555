# PCA9555 Basic ESP-IDF Example

This is a minimal diagnostic ESP-IDF project for the framework-neutral PCA9555
core. It is not a production shared-bus manager and it is not hardware validation
by itself.

The example demonstrates:

- application-owned I2C bus initialization,
- native ESP-IDF I2C callbacks for `Config::i2cWrite` and
  `Config::i2cWriteRead`,
- timeout propagation from `Config::i2cTimeoutMs` to the IDF I2C transaction,
- a recursive FreeRTOS mutex shared by transport callbacks and the optional
  `Config::i2cLock` / `Config::i2cUnlock` hooks,
- `esp_err_t` to `PCA9555::Status` mapping inside the example adapter,
- safe optional output enabling through `configureOutputs()`.

The default pins are diagnostic defaults:

- SDA: GPIO 8
- SCL: GPIO 9
- I2C port: `I2C_NUM_0`
- Bus speed: 400 kHz
- PCA9555 address: `0x20`

Check these against your board before connecting hardware.

## Build

```bash
idf.py -C examples/esp_idf/basic set-target esp32s3 build
idf.py -C examples/esp_idf/basic set-target esp32s2 build
```

The project expects the ESP-IDF v5.4 line. The component itself does not include Arduino
or `Wire`.

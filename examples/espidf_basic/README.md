# PCA9555 ESP-IDF CLI Example

This is a native ESP-IDF bring-up CLI for the PCA9555 driver. It uses
`app_main()`, the ESP-IDF `driver/i2c_master.h` API, `esp_timer`, `vTaskDelay`,
and fixed C buffers. It does not include Arduino, `Wire`, or Arduino
compatibility shims.

Build:

```bash
idf.py -C examples/espidf_basic set-target esp32s3 build
idf.py -C examples/espidf_basic set-target esp32s2 build
```

The example owns the I2C bus setup and adapts native ESP-IDF I2C calls to
`PCA9555::Status`. It is example code, not a production bus manager. Review SDA,
SCL, address straps, pull-ups, INT wiring, supply voltage, and output loads
before connecting hardware.

Commands that write outputs, change direction or polarity, write raw registers,
run patterns, recover state, self-test, or stress mixed operations require a
final `confirm` token. Without `confirm`, the CLI prints the pending change and
the confirmed command form.

This example is not hardware validation. Passing the static contract or
compiling the example still needs real serial logs, wiring notes, and bench
evidence recorded in the hardware validation matrix.

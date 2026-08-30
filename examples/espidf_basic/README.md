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

The CLI configures the primary console selected by ESP-IDF sdkconfig for
blocking input before entering its fixed-buffer `fgets()` loop. The default
UART console, USB Serial/JTAG, and USB CDC are supported. Select a non-default
primary console with `idf.py -C examples/espidf_basic menuconfig` before the
build; an output-only secondary console cannot accept commands.

The example owns the I2C bus setup and maps each native ESP-IDF I2C call to one
terminal `PCA9555::TransportResult`. It calls passive `bind()` first and then
uses ESP-IDF's address-only probe for an exact ACK/NACK result without changing
driver health. It is example code, not a production bus manager. Review SDA,
SCL, address straps, pull-ups, INT wiring, supply voltage, and output loads
before connecting hardware.

Commands that write outputs, change direction or polarity, write raw Output or
Polarity registers (`0x02` through `0x05`),
run patterns, apply the example recovery image, self-test, or stress mixed
operations require a final `confirm` token. Without `confirm`, the CLI prints
the pending change and the confirmed command form. The `recover` command is
application policy: it applies high output latches, normal polarity, and all
pins input through the cooperative apply-image API. It does not recover the I2C
bus and does not invoke a library-owned retry policy.
The read-only `stress` command does not require confirmation.

This example is not hardware validation. Native ESP-IDF remains a build/contract
target, while native-IDF hardware qualification is outside the current release
scope and was not performed. Arduino ESP32-S3 HIL does not validate this native
runtime path.

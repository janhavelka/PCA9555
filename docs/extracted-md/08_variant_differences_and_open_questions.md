# Variants And Open Questions

Package options listed in the datasheet include SSOP, TVSOP, SOIC, and VQFN body sizes; logical register behavior is the same for PCA9555. Source: datasheet, p. 1.

Compatibility notes:

- The datasheet says the PCA9555 is pin-to-pin and I2C-address compatible with PCF8575, but software changes are required because PCA9555 has command registers. Source: datasheet, p. 14.
- All PCA9555 addresses are in `0x20-0x27`, selected by A2/A1/A0. Source: datasheet, p. 19.
- The supplemental auto-increment note is general I2C background; PCA9555-specific pair behavior must come from the PCA9555 datasheet. Source: auto-increment app note, pp. 4-5; PCA9555 datasheet, pp. 21-23.

Not documented in PDFs / repository policy choices:

- The PDFs do not define a preferred software API shape for byte-port versus 16-bit operations.
- The PDFs document the INT errata and input-read clearing behavior, but do not prescribe whether interrupt service code must read both input ports.
- The PDFs do not define a software cache policy for output latch read/modify/write helpers.
- The PDFs state PCF8575 pin/address compatibility, but do not define compatibility API names.

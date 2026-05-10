# Document Inventory

These compact notes summarize PCA9555 facts from the PDF sources. Raw OCR/PDF extraction is kept in `docs/pdf-extracted-md`; `docs/extracted-md` is the curated note set.

| Source PDF | Raw extract | Pages used | Notes |
|---|---|---:|---|
| `docs/PCA9555-Remote-16-bit-I2C-SMBus-IO-Expander-Data-Sheet-SCPS131J.pdf` | `docs/pdf-extracted-md/PCA9555-Remote-16-bit-I2C-SMBus-IO-Expander-Data-Sheet-SCPS131J.md` | 1-49 | Primary source for pinout, electrical limits, address map, registers, transactions, interrupt behavior, and POR notes. |
| `docs/application_notes/auto_increment_feature.pdf` | `docs/pdf-extracted-md/auto_increment_feature.md` | 1-6 | Supplemental source for general I2C auto-increment context; use only as background, not as PCA9555-specific register truth. |

Compact note set:

| File | Purpose |
|---|---|
| `01_chip_overview.md` | Device role and capabilities. |
| `02_pinout_and_signals.md` | Address pins, I/O ports, INT, SDA/SCL, supply pins. |
| `03_electrical_and_timing.md` | Supply, I2C speed, current, pull-up, timing limits. |
| `04_protocol_commands_and_transactions.md` | Address byte, command byte, reads/writes, auto-increment pair behavior. |
| `05_register_map.md` | Eight command registers and defaults. |
| `06_modes_interrupts_status_and_faults.md` | INT behavior, errata, POR, and status model. |
| `07_initialization_reset_and_operational_notes.md` | PCA9555 bring-up and safe update notes. |
| `08_variant_differences_and_open_questions.md` | Package/compatibility notes and unresolved choices. |

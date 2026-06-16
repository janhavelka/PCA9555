# PCA9555 Documentation

This folder keeps durable project documentation only. User-facing usage lives in
the repository README, runnable behavior lives in `examples/`, and API details
live in Doxygen comments under `include/`.

## Documents

- [Register reference](register_reference.md): PCA9555 register map, pair
  auto-increment behavior, direct access rules, interrupt notes, and errata
  summary.
- [Hardware validation](hardware_validation.md): HIL runner usage, target run
  template, evidence checklist, and hardware validation matrix.
- [ESP-IDF notes](espidf.md): native ESP-IDF component/example boundary,
  transport ownership, and static contract checks.
- [Release checklist](release.md): merge, release-candidate, and
  production-claim gates.

## External References

- [TI PCA9555 product page](https://www.ti.com/product/PCA9555)
- [TI PCA9555 datasheet](https://www.ti.com/lit/ds/symlink/pca9555.pdf)

## Removed From Permanent Docs

Prompt audit reports, merge reconciliation notes, generated PDF extractions,
and one-off implementation progress reports are intentionally not kept here.
Those are useful during development, but they make the docs harder to scan after
the decisions have been folded into README, tests, and API comments.

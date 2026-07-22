# PCA9555 Documentation

This folder keeps durable public project documentation. The repository
[README](../README.md) owns lifecycle, transport, cooperative-operation,
error-handling, and migration guidance. Runnable behavior lives in `examples/`,
and API contracts live in Doxygen comments under `include/PCA9555/`.

## Documents

- [Register reference](register_reference.md): PCA9555 register map, pair
  auto-increment behavior, direct access rules, interrupt notes, and errata
  summary.
- [Chip notes](chip_notes.md): durable electrical, reset, current, interrupt,
  and layout facts extracted from the source chip documentation.
- [Hardware validation](hardware_validation.md): HIL runner usage, target run
  command contract, known serial-channel constraint, and open hardware gates.
- [ESP-IDF notes](espidf.md): native ESP-IDF component/example boundary,
  transport ownership, and static contract checks.
- [Release status](release.md): remaining real-target work and allowed claims.
- [Changelog](../CHANGELOG.md): versioned API and behavior history.
- [Security policy](../SECURITY.md): supported-version and vulnerability
  reporting policy.
- [Contributing guide](../CONTRIBUTING.md): engineering and validation gates.

## Generated API Reference

Run `doxygen Doxyfile` from the repository root, then open
`docs/doxygen/html/index.html`. The generated tree is ignored by Git and is not
part of the public package. Doxygen reads public headers and the durable guides
listed above. It deliberately excludes example implementation internals and
the integration audit so the API reference stays focused.

Missing public-symbol documentation and Doxygen documentation errors fail the
generation command. Parameter names and types remain visible in each generated
signature; longer ownership, timing, and failure contracts stay next to their
public declarations and in the root README.

The source repository also retains the unfinished external adoption work in
`docs/tunnelmonitor-node-suitability-audit.md`. It is repository-only project
context and is excluded from the package and generated API manual.

## External References

- [TI PCA9555 product page](https://www.ti.com/product/PCA9555)
- [TI PCA9555 datasheet](https://www.ti.com/lit/ds/symlink/pca9555.pdf)

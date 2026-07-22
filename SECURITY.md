# Security Policy

## Supported versions

| Version | Security fixes |
| --- | --- |
| 3.x after publication | Supported |
| 3.0.0 working-tree candidate | Not yet released |
| 2.x and older | Not supported |

Pin an exact reviewed release or immutable commit. Moving branches are not a
safe production dependency policy. The current 3.0.0 source metadata does not
mean that a `v3.0.0` release has already been published.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Email
`info@thymos.cz` with:

- the affected version or commit;
- a clear description and impact;
- reproducible steps or a minimal test;
- relevant target, framework, and transport details;
- a suggested fix, if available.

Do not include production credentials, private device data, or destructive
hardware instructions.

## Scope

The library is a non-owning PCA9555 chip driver. It has no network stack,
persistent storage, dynamic plugin loading, credential handling, or bus
recovery authority.

Security and safety relevant properties include:

- fixed-capacity state and no steady-state heap allocation;
- terminal, timeout-bounded transport callbacks;
- explicit error results and conservative uncertain-write handling;
- bounded cooperative compound operations;
- no hidden retry or fake-success path;
- passive lifecycle with no implicit hardware mutation;
- caller-owned serialization, retry policy, health policy, and bus recovery.

The application remains responsible for validating untrusted commands, limiting
access to hardware-control interfaces, watchdog policy, electrical safety, and
dependency review.

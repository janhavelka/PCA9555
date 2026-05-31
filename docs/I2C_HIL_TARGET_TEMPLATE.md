# PCA9555 I2C HIL Target Template

No physical HIL validation was performed by creating this template. A target run
becomes evidence only after the operator fills it in and attaches the generated
`hil_logs/` artifacts. I2C ACK proves address response only and is not PCA9555
chip identity.

## Target Profile

| Field | Value |
| --- | --- |
| Operator | |
| Date/time | |
| Branch | |
| Commit hash | |
| Worktree dirty/clean state | |
| MCU board | |
| Build environment | |
| Serial port | |
| Baud rate | 115200 |
| Device/module | |
| PCA9555 chip marking | |
| I2C address | |
| I2C speed | |
| Supply voltage | |
| Pull-ups | |
| Reset wiring | Power-cycle only |
| Interrupt wiring | |
| Safe load/current limit | |
| Evidence directory | `hil_logs/i2c_<timestamp>/` |

## Exact Commands

Build:

```bash
python -m platformio run -e <env>
```

Upload:

```bash
python -m platformio run -e <env> --target upload --upload-port <PORT>
```

Monitor:

```bash
python -m platformio device monitor --port <PORT> --baud 115200
```

HIL runner:

```bash
python tools/run_i2c_hil.py --port <PORT> --baud 115200 --address 0x20
```

Dry-run planning:

```bash
python tools/run_i2c_hil.py --dry-run
```

## Default Command Sequence

<!-- HIL_COMMAND_SEQUENCE_START -->
- `version`
- `help`
- `scan`
- `probe`
- `cfg`
- `read`
- `outputs`
- `config`
- `polarity`
- `dump`
- `pins`
- `drv`
- `stress 10`
- `drv`
<!-- HIL_COMMAND_SEQUENCE_END -->

## Evidence Checklist

| Item | Status | Evidence path |
| --- | --- | --- |
| `serial_transcript.txt` captured | NOT RUN | |
| `summary.md` captured | NOT RUN | |
| `summary.json` captured | NOT RUN | |
| `operator_checklist.md` completed | NOT RUN | |
| Wiring photo attached | NOT RUN | |
| Address strap table attached | NOT RUN | |
| Safe load/current limit documented | NOT RUN | |
| Per-pin input observation recorded | OPERATOR_CHECK_REQUIRED | |
| Physical output observation recorded | OPERATOR_CHECK_REQUIRED | |
| INT assertion/clear capture attached | OPERATOR_CHECK_REQUIRED | |
| Errata pointer-park I2C decode attached | OPERATOR_CHECK_REQUIRED | |
| Fault injection evidence attached | OPERATOR_CHECK_REQUIRED | |

## Matrix Mapping

| Matrix Row | Result | Evidence path | Notes |
| --- | --- | --- | --- |
| 1. Address scan/probe | NOT RUN | | |
| 2. POR defaults | NOT RUN | | |
| 3. Input reads | OPERATOR_CHECK_REQUIRED | | |
| 4. Output writes | OPERATOR_CHECK_REQUIRED | | Requires opt-in output tests |
| 5. Bulk mask write | OPERATOR_CHECK_REQUIRED | | Requires safe loads |
| 6. Latch preload ordering | OPERATOR_CHECK_REQUIRED | | Requires logic analyzer |
| 7. Output-to-input interrupt behavior | OPERATOR_CHECK_REQUIRED | | Requires INT capture |
| 8. Polarity inversion | OPERATOR_CHECK_REQUIRED | | Requires known physical levels |
| 9-12. INT and errata checks | OPERATOR_CHECK_REQUIRED | | Requires analyzer/capture |
| 13-15. Fault/recovery checks | OPERATOR_CHECK_REQUIRED | | Requires safe handling |
| 16-18. Soak and bus-speed checks | NOT RUN | | Optional flags and target setup |
| 19-21. Target-specific hardware runs | NOT RUN | | |

## Claim Summary

This record supports only the rows with attached evidence. It must not be used
to claim production-ready, industry-grade, field-proven, or hardware validated
status unless the release checklist and hardware matrix are fully completed and
reviewed.

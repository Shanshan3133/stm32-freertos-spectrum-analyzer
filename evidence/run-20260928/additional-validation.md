# 2026-09-28 additional board validation

Board: NUCLEO-F446RE (MB1136 rev C), ST-LINK V2.1, COM6 at 921600 baud.
Loopback wiring: PA4/A2 to PA0/A0 and PA1/A1 through one breadboard strip.
The normal firmware used the ST-LINK MCO clock build. These results were
observed before the corresponding local source changes were committed.

## Continuous loopback, user-operated

Command: `python tools/endurance_check.py --port COM6 --seconds 1800 --progress-seconds 60`.
The user supplied the full terminal output. The final summary reported:
The [terminal transcript](endurance-terminal.txt) is retained alongside this
summary; only the local Windows prompt containing a username was removed.

| Metric | Observed |
|---|---:|
| Elapsed | 1,800.071 s |
| Frames, channels 0 / 1 | 35,998 / 35,999 |
| Output rates, channels 0 / 1 | 19.998 / 19.999 Hz |
| Correct tone, channels 0 / 1 | 35,998 / 35,999 frames |
| Maximum / p99.9 processing time | 1,742 / 1,731 us |
| 9 ms deadline misses | 0 |
| CRC / format errors | 0 / 0 |
| Sequence lost / incomplete evictions | 0 / 0 |
| Maximum reported dropped blocks | 0 |
| Timestamp discontinuities | 0 |
| Reported status | `0x00000203` in all 71,997 frames |
| Script result | `pass: true` |

The host-clock estimate was 99,995.19 samples/s from board timestamps and
PC receive time. This is an estimate, not an oscilloscope or logic-analyzer
measurement of the ADC trigger clock. The maximum processing time is the
maximum observed in this run, not a proven worst-case execution time.

## Controlled ADC-freeze fault, agent-operated

The dedicated `-FreezeAdcFault` binary was built, flashed at `0x08000000`,
read-back verified, and observed for 5.092 s with `serial_smoke.py`.
There were 103 / 102 frames on channels 0 / 1, zero CRC/format/sequence
errors, zero reported drops, and all 205 frames had status `0x00006A01`.
That status contains `ADC_RUNNING`, `TEST_SIGNAL`, `SIGNAL_WEAK`,
`SIGNAL_FROZEN`, and `FAULT_INJECTED`; `SIGNAL_VALID` was clear. This was
software fault injection, not a physical input disconnection.

The normal ST-LINK MCO firmware was then reflashed and read-back verified.
A 10.042 s loopback check passed: 200 frames on each channel, all at the
expected bin-10 peak, zero CRC/format/sequence errors or reported drops,
and normal status `0x00000203` in all 400 frames.

## Live visual snapshot

After the endurance run, one paired telemetry result was captured from the
normal firmware using `tools/capture_loopback_evidence.py`. The
[spectrum image](spectrum-snapshot.png) plots both channels. The
[full 512-bin CSV](spectrum-snapshot.csv), [waveform preview CSV](waveform-snapshot.csv),
and [capture metadata](snapshot-metadata.json) permit inspection of the
underlying decoded values. This snapshot is not a 30-minute aggregate or an
independent timebase measurement. A [hardware photograph](hardware-loopback.jpg)
shows the loopback setup.

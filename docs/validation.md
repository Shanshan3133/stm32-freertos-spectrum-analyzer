# Hardware acceptance checklist

Acceptance targets remain pending unless a result is explicitly marked as
observed. On 2026-09-27 the NUCLEO-F446RE was flashed and run without DAC-to-ADC
jumpers. A 60 s VCP smoke run yielded 1194 complete channel-0 and 1193 complete
channel-1 spectra, zero CRC/format/sequence errors, zero reported dropped
blocks, and a maximum reported `processing_us` of 1,812. Both ADC inputs were
floating, so this run establishes boot, task progress, UART framing, and basic
DMA-path liveness—not tone accuracy, analog validity, independent 100 kS/s
timing, or worst-case execution time. A separate injected DSP stall produced
IWDG resets and the reset-cause status bit; normal firmware was restored and
smoke-tested again. The test commands/results are in
[`evidence/run-20260927/metadata.md`](../evidence/run-20260927/metadata.md).

On 2026-09-28 the DAC-to-dual-ADC breadboard loopback passed a 60 s check:
1194/1193 complete spectra, bin-10 peak in every frame, 976.563 Hz median
nominal-frequency estimate on both channels, zero decoder/sequence errors and
zero reported dropped blocks. This confirms the loopback signal path, not an
independently measured 100 kS/s clock. See the
[`loopback evidence`](../evidence/run-20260928/metadata.md).

A later 1,800.071 s continuous loopback run received 35,998 and 35,999
spectra on channels 0 and 1, respectively (19.998/19.999 Hz), with zero
CRC/format/sequence errors, zero reported dropped blocks, and zero measured
9 ms deadline misses. The maximum observed DSP time was 1,742 us and p99.9
was 1,731 us. The host-versus-board timestamp estimate of 99,995.19 samples/s
is not an independent instrument measurement. A separate injected ADC-freeze
run produced weak/frozen/injected status with `SIGNAL_VALID` clear in 205
frames. After restoring normal firmware, a 10 s loopback check passed with
200 frames per channel. See
[`additional validation evidence`](../evidence/run-20260928/additional-validation.md).

## Remaining board priorities

1. Independently measure TIM2/DAC/ADC timing with an external instrument.
   A logic analyzer cannot measure analog voltage; use an oscilloscope for PA4
   when available. The shared-clock loopback cannot prove absolute frequency.
2. Measure task stack high-water marks and independently corroborate the
   DWT-derived DSP timing. The 30-minute run and spectrum snapshot are complete.
3. In a separate controlled test, power off before changing the loopback
   connection, then verify signal-fault reporting and recovery. Floating ADC
   pins alone may spuriously appear valid, so do not assume this test passes.

| Test | Method | Pass criterion |
|---|---|---|
| Sampling rate | GPIO marker at DMA half callback, 60 s | 10.240 ms block period within 0.1%; no gaps |
| Dual-channel path | PA4 wired to PA0 and PA1 | observed 2026-09-28: both peaked at bin 10 for all 2,387 frames in 60 s |
| FFT deadline | DWT cycle count and DSP GPIO pulse | observed DWT-derived maximum 1.742 ms in 30 min with zero deadline misses; GPIO pulse measurement pending |
| Spectrum output | Python monitor for 30 min | observed 2026-09-28: 1,800.071 s, 19.998/19.999 results/s, zero sequence loss |
| Protocol faults | Python unit suite | malformed/CRC/truncated/escape cases recover |
| Timestamp wrap | run beyond 24 s and inject values around u32 wrap | no false gap at the former DWT wrap; wrap-safe subtraction |
| DMA ownership | delay DSP beyond one block in fault build | stale half-buffer rejected and flagged |
| Signal quality | injected ADC freeze | observed 2026-09-28: weak/frozen flags set and valid bit cleared in 205 frames; physical disconnect/ground/clipping remain open |
| UART saturation | worst-case escaped test data | no transmit timeout or flagged drop |
| Stack margin | `uxTaskGetStackHighWaterMark()` after stress | at least 25% free per task |
| Watchdog | compile-time DSP-stall injection | observed 2026-09-27: IWDG reset and reset-cause bit; measure recovery latency separately |
| Idle behavior | timing trace around `WFI` or debugger counter | CPU enters sleep between runnable work |

Record board revision, firmware commit, compiler version/options, clock
configuration, test duration, and raw CSV/logic traces. A logic analyzer shows
GPIO and UART activity, not DMA itself; GPIO markers establish the relationship
between DMA completion and task execution.

Suggested evidence files after the board arrives:

```text
evidence/run-YYYYMMDD/metadata.md
evidence/run-YYYYMMDD/telemetry.csv
evidence/run-YYYYMMDD/dma-timing.sr
evidence/run-YYYYMMDD/stack-and-wcet.md
evidence/run-YYYYMMDD/spectrum.png
```

Use `--csv` for per-frame summaries, `--spectrum-csv` for all 512 frequency
bins, and `--waveform-csv` for all 128 preview samples.

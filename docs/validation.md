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

## First-board priority

1. Confirm TIM2-triggered DAC and the two simultaneous ADC paths. Capture the
   DAC waveform plus DMA callback timing GPIO; a logic analyzer cannot measure
   analog voltage, so use an oscilloscope for PA4 when available.
2. Read the per-frame DWT-derived `processing_us`, capture the maximum over a
   sustained run, and compare it with the 9 ms deadline in `timing_budget.md`.
3. Plot the expected 976.5625 Hz DAC tone in the Python spectrum view and save
   the complete spectrum CSV plus screenshot.
4. Disconnect PA4-to-PA0 while running, confirm weak/frozen status and loss of
   `STATUS_SIGNAL_VALID`, reconnect it, and confirm recovery on the next valid
   result.

| Test | Method | Pass criterion |
|---|---|---|
| Sampling rate | GPIO marker at DMA half callback, 60 s | 10.240 ms block period within 0.1%; no gaps |
| Dual-channel path | PA4 wired to PA0 and PA1 | both detect 976.5625 Hz; frequency error < one FFT bin |
| FFT deadline | DWT cycle count and DSP GPIO pulse | maximum processing time < 9.0 ms |
| Spectrum output | Python monitor for 30 min | 20 results/s/channel; no unexpected sequence loss |
| Protocol faults | Python unit suite | malformed/CRC/truncated/escape cases recover |
| Timestamp wrap | run beyond 24 s and inject values around u32 wrap | no false gap at the former DWT wrap; wrap-safe subtraction |
| DMA ownership | delay DSP beyond one block in fault build | stale half-buffer rejected and flagged |
| Signal quality | disconnect, ground, then overdrive within safe limits | weak/frozen/clipping flags; valid bit cleared |
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

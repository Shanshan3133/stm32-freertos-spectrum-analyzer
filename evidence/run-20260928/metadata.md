# 2026-09-28 DAC-to-dual-ADC loopback

The user connected NUCLEO-F446RE A2/PA4 (DAC_OUT1), A0/PA0 (ADC1_IN0), and
A1/PA1 (ADC2_IN1) to one connected strip of a mini breadboard. The board was
running the normal firmware from commit `d88b60a`, previously programmed and
read-back verified. ST-LINK VCP enumerated as COM6 at 921600 8-N-1.

Command: `python tools/loopback_check.py --port COM6 --seconds 60` with
pyserial 3.5. The program discards the partial serial frame present at opening,
then counts all subsequent decoder and sequence errors. Output:

```json
{
  "elapsed_s": 60.066,
  "expected_hz": 976.5625,
  "expected_peak_bin": 10,
  "chunks": 9550,
  "channels": {
    "0": {
      "frames": 1194, "rate_hz": 19.88,
      "dominant_hz_median": 976.563,
      "dominant_hz_min": 976.503, "dominant_hz_max": 976.622,
      "rms_median": 0.28299, "rms_min": 0.28281, "rms_max": 0.2832,
      "tone_and_peak_bin_pass": 1194,
      "processing_us_max": 1752, "dropped_blocks_max": 0
    },
    "1": {
      "frames": 1193, "rate_hz": 19.86,
      "dominant_hz_median": 976.563,
      "dominant_hz_min": 976.503, "dominant_hz_max": 976.622,
      "rms_median": 0.28287, "rms_min": 0.28268, "rms_max": 0.28308,
      "tone_and_peak_bin_pass": 1193,
      "processing_us_max": 1752, "dropped_blocks_max": 0
    }
  },
  "status_counts": {"0x00000203": 2387},
  "crc_errors": 0, "format_errors": 0, "sequence_lost": 0,
  "incomplete_evictions": 0, "pass": true
}
```

This verifies both analog loopback channels, DMA/acquisition-to-DSP-to-UART
progress, the expected spectral peak bin, and short-run transport integrity.
The independent FFT sample-rate accuracy is **not** established: DAC and ADC
share TIM2, so a clock-rate error moves both together and still yields bin 10.
The reported frequency is inferred from the configured nominal 100 kS/s rate,
not measured against an independent time reference. Likewise, 1,752 us is the
maximum reported during this 60 s run, not a proven worst-case execution time.
This file describes only the initial 60 s run. The later
[30-minute endurance and injected-freeze results](additional-validation.md),
plus a [live spectrum snapshot](spectrum-snapshot.png), are recorded
separately. External timing measurement, stack headroom, and physical signal
disconnect/recovery tests remain open.

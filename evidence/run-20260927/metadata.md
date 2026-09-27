# 2026-09-27 initial board validation — no loopback wiring

Board: NUCLEO-F446RE (MB1136 rev C marking), ST-LINK V2.1 firmware
V2J33M25, STM32F446xx rev A, 512 KiB flash. STM32CubeProgrammer 2.23.0
reported 3.24 V. Windows virtual COM port: COM6. ARM GCC 14.3.1 from
STM32CubeIDE 2.2.0. Official STM32CubeF4 checkout:
`5723be54530654c81a1c1ef9da39b2ce1c40564c` with the pinned HAL,
device, and FreeRTOS submodules. Both ADC inputs were **unconnected/floating**.

Target build: `powershell -ExecutionPolicy Bypass -File tools/build_target.ps1`.
Linker size: text 103,864 B; data 96 B; BSS 63,248 B. The normal `.bin` was
programmed at `0x08000000`, verified byte-for-byte by STM32CubeProgrammer,
and reset. The output used USART2 DMA through ST-LINK VCP at 921600 8-N-1.

`python tools/serial_smoke.py --port COM6 --seconds 60` produced:

```json
{
  "elapsed_s": 60.061,
  "chunks": 9549,
  "frames_by_channel": {"0": 1194, "1": 1193},
  "rate_hz_by_channel": {"0": 19.88, "1": 19.86},
  "crc_errors": 0,
  "format_errors": 0,
  "sequence_lost": 0,
  "incomplete_evictions": 0,
  "dropped_blocks_max": 0,
  "processing_us_max": 1812,
  "status_counts": {"0x00000203": 2387}
}
```

`0x203` comprises `ADC_RUNNING`, `SIGNAL_VALID`, and `TEST_SIGNAL`. Here
`TEST_SIGNAL` means DAC generation was enabled, **not** that its output reached
the ADC pins. Floating pins can spuriously pass the current signal-valid
heuristic; this status must not be used as loopback evidence.

For watchdog recovery, a separate build used
`tools/build_target.ps1 -WatchdogFaultAfterBlocks 100`, programmed with verify,
and sampled for six seconds. The decoded status was `0x283` (includes
`STATUS_WATCHDOG_RESET`) on 96 complete spectra, with two downward timestamp
jumps consistent with repeated MCU resets and zero decoder errors. This
supports the IWDG reset-cause path, but does not measure exact recovery latency.
The normal build (fault option 0) was then reprogrammed using SWD under reset,
verified, and smoke-tested again for ten seconds: 200 frames/channel,
zero CRC/format/sequence errors, zero reported drops, and status `0x203`.

Remaining: connect PA4/A2 to PA0/A0 and PA1/A1 while power is off; verify the
976.5625 Hz tone, amplitude, true trigger timing, stack margin, 30-minute
endurance, and optional external captures. No analog or worst-case timing
claim follows from this no-wire run.

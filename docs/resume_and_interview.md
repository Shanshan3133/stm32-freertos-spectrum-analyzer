# Resume and interview wording

This document separates claims that are defensible now from claims that require
physical target evidence. Keep that distinction in applications and interviews.

## Current resume version: flashed and smoke-tested; loopback pending

**FreeRTOS Dual-Channel Real-Time Spectrum Analyzer | C, Python, STM32F446**

- Built and flashed a dual-channel STM32F446 spectrum-analyzer firmware using
  timer-triggered dual ADC, DMA ping-pong buffering, and a priority-scheduled
  FreeRTOS acquisition/DSP/telemetry pipeline.
- Developed a 1024-point Q15 FFT pipeline with DC removal, Hann windowing, RMS,
  peak and interpolated-frequency estimation; quantified fixed-point error
  against a floating-point reference across 12 signal cases.
- Host-tested a CRC-protected binary protocol and bounded streaming decoder
  with 21 Python fault-injection tests covering corruption, truncation,
  escaping, loss, duplication, reordering, and incomplete-frame recovery.
- Added progress-based watchdog voting, DMA ownership checks, sticky fault
  diagnostics, UART bandwidth analysis, and CI that builds and executes the
  portable C test suite.
- Smoke-tested the board over its ST-LINK virtual COM port: 2,387 complete
  spectra in 60 s, zero CRC/format/sequence errors or reported dropped blocks;
  confirmed watchdog reset reporting with an injected DSP stall.

Do not claim verified 100 kS/s analog sampling accuracy, 976.5625 Hz detection,
worst-case deadline, or watchdog recovery latency yet. The 100 kS/s trigger rate and
9 ms deadline are configured targets until independent loopback/timing
acceptance is complete. The smoke test used floating ADC inputs.

## Short project description for an application form

Built and flashed a dual-channel FreeRTOS spectrum-analyzer firmware for
STM32F446, combining timer-triggered ADC/DMA buffering, a CMSIS-DSP Q15 FFT,
watchdog-supervised tasks, CRC-protected telemetry, Python visualization, fault
injection, and CI. Initial on-board serial smoke and injected watchdog reset
tests passed; DAC-to-ADC
loopback accuracy and long-run timing/endurance measurements remain pending.

## Interview answer: what target evidence exists

> I built and flashed the target firmware on NUCLEO-F446RE. With no external
> jumpers, I observed correctly framed dual-channel telemetry over the ST-LINK
> serial bridge with no reported drops in a 10-second smoke run. That proves
> boot and basic data-path progress, but the ADC pins were floating. I have not
> yet claimed measured sampling accuracy, tone detection, true WCET, or
> watchdog recovery; those require the documented loopback and fault tests.

## Upgrade after hardware acceptance

Only after each relevant row in `validation.md` has dated evidence, add the
actual measured frequency error, maximum or p99.9 DSP time, endurance
duration, dropped-block count, and stack margin to the resume.

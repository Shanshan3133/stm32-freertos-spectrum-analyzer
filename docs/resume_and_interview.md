# Resume and interview wording

This document separates claims that are defensible now from claims that require
physical target evidence. Keep that distinction in applications and interviews.

## Current resume version: flashed and loopback-tested

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
- Verified DAC-to-dual-ADC loopback on the board: both channels detected FFT
  bin 10 in all 2,387 spectra during a 60-second run, with no CRC/sequence
  errors or reported dropped blocks; separately confirmed IWDG reset reporting
  with an injected DSP stall.

Do not claim independently verified 100 kS/s clock accuracy, worst-case
deadline, 30-minute endurance, or watchdog recovery latency yet. The DAC and
ADCs share one timer, so observing bin 10 at a reported 976.563 Hz establishes
the closed signal path but not absolute timebase accuracy. The 9 ms DSP
deadline remains a design target pending sustained timing validation.

## Short project description for an application form

Built and flashed a dual-channel FreeRTOS spectrum-analyzer firmware for
STM32F446, combining timer-triggered ADC/DMA buffering, a CMSIS-DSP Q15 FFT,
watchdog-supervised tasks, CRC-protected telemetry, Python visualization, fault
injection, and CI. A 60-second on-board DAC-to-dual-ADC loopback run detected
the expected FFT bin on both channels without protocol errors or reported
dropped blocks. Independent sampling-clock, WCET and endurance measurements
remain pending.

## Interview answer: what target evidence exists

> I built and flashed the target firmware on NUCLEO-F446RE. With DAC_OUT1
> connected to both ADC inputs, both channels reported the expected FFT peak
> throughout a 60-second run without transport errors or reported drops. I
> also observed the IWDG reset-cause flag after deliberately stalling DSP.
> Because the DAC and ADC share TIM2, this is a closed-path test, not an
> independent measurement of the 100 kS/s clock or a WCET claim.

## Upgrade after hardware acceptance

Only after each relevant row in `validation.md` has dated evidence, add the
actual measured frequency error, maximum or p99.9 DSP time, endurance
duration, dropped-block count, and stack margin to the resume.

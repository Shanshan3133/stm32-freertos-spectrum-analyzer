# Resume and interview wording

This document separates claims that are defensible now from claims that require
physical target evidence. Keep that distinction in applications and interviews.

## Current resume version: host-verified, target pending

**FreeRTOS Dual-Channel Real-Time Spectrum Analyzer | C, Python, STM32F446**

- Implemented the firmware architecture for a 100 kS/s-per-channel analyzer
  using timer-triggered dual ADC, DMA ping-pong buffering, and a
  priority-scheduled FreeRTOS acquisition/DSP/telemetry pipeline.
- Developed a 1024-point Q15 FFT pipeline with DC removal, Hann windowing, RMS,
  peak and interpolated-frequency estimation; quantified fixed-point error
  against a floating-point reference across 12 signal cases.
- Host-tested a CRC-protected binary protocol and bounded streaming decoder
  with 21 Python fault-injection tests covering corruption, truncation,
  escaping, loss, duplication, reordering, and incomplete-frame recovery.
- Added progress-based watchdog voting, DMA ownership checks, sticky fault
  diagnostics, UART bandwidth analysis, and CI that builds and executes the
  portable C test suite.

Do not use `measured`, `achieved`, `validated on STM32`, or `ran at 100 kS/s`
yet. The 100 kS/s rate and 9 ms deadline are design targets until hardware
acceptance is complete.

## Short project description for an application form

Designed and host-validated the software architecture of a dual-channel
FreeRTOS spectrum analyzer for STM32F446. The project combines timer-triggered
ADC/DMA buffering, a CMSIS-DSP Q15 FFT backend, watchdog-supervised tasks, a
CRC-protected telemetry protocol, Python visualization, fault injection, and
automated CI. Target-board timing and endurance measurements are explicitly
tracked as pending because the development board was unavailable before the
application deadline.

## Interview answer: why target evidence is pending

> I completed the firmware architecture, portable DSP reference, protocol,
> receiver, fault injection, and automated host verification. The target board
> became unavailable before my application deadline, so I did not present the
> 100 kS/s rate or WCET budget as measured results. I documented an acceptance
> plan using DAC-to-ADC loopback, DWT cycle timing, stack high-water marks, and
> watchdog reset injection, which I can execute without changing the system
> architecture when the board is available.

## Upgrade after hardware acceptance

Only after every relevant row in `validation.md` has dated evidence, replace
the first bullet's `Implemented the firmware architecture for` with `Built`,
and replace `Host-tested` with the actual measured frequency error, maximum or
p99.9 DSP time, endurance duration, dropped-block count, and stack margin.

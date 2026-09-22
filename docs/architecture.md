# Firmware architecture

## Data path

`TIM2 TRGO -> ADC1 + ADC2 -> DMA circular halves -> acquisition task -> DSP
task -> latest-result queue -> telemetry task -> USART2 DMA -> ST-LINK VCP`

The DMA buffer contains 2048 packed 32-bit words. Each word holds ADC1 in bits
0..11 and ADC2 in bits 16..27. A half/full callback publishes one immutable
1024-frame region. Processing must finish before DMA wraps back to that region.

| Priority | Task | Activation | Responsibility |
|---:|---|---|---|
| 6 | watchdog | 250 ms | feed IWDG only after all critical votes |
| 5 | acquisition | DMA half/full event | continuity checks and descriptor queue |
| 4 | DSP | queued 1024-frame block | deinterleave, Hann window, FFT, metrics |
| 3 | telemetry | latest result, limited to 20 Hz | frame, CRC, USART2 DMA |

The block queue has two descriptors. The result queue has one full result and
uses overwrite semantics, so congestion sacrifices old display data rather
than acquisition freshness. Drops and overruns are sticky status flags.

## Signal processing

The workstation backend uses a portable radix-2 floating-point FFT for test
repeatability. The target build defines `ANALYZER_USE_CMSIS_DSP` and compiles
`platform/stm32f446/spectrum_cmsis.c` for the CMSIS-DSP Q15 real FFT. Both
implement the same interface and produce RMS, peak, 512 magnitudes, and a
parabolically interpolated dominant frequency.

For every channel, the application first calculates the 1024-sample mean and
subtracts it while converting the ADC block to Q15. Only then does the selected
backend apply the Hann window and execute the FFT. The exact order is therefore
`block DC removal -> Hann -> FFT`, preventing DC offset from being shaped into
low-frequency leakage by the window.

Q15 spectra remain at full resolution inside the MCU. Only UART spectrum bins
are quantized to 8 bits. This bounds worst-case escaped traffic while leaving
RMS, peak, and dominant frequency at their original precision.

## Self-test and recovery

DAC1 outputs a 1024-sample sine table at FFT bin 10. TIM2 triggers DAC1 and both
ADCs from the same 100 kHz event. Two jumpers may fan PA4 out to PA0 and PA1,
testing both channels against the same coherent source.

The acquisition monitor checks block size, generation sequence, and timestamp
spacing. TIM5 supplies a 1 MHz 32-bit timebase. The DSP validates the DMA
generation before and after copying samples, then checks deadline, minimum RMS,
clipping, frozen input, and Nyquist plausibility. USART DMA timeouts set
backpressure status. The independent watchdog is refreshed only when
acquisition, DSP, and telemetry have each processed real pipeline data. A DMA
wait or queue receive timeout does not cast a health vote, so an upstream stall
cannot be hidden by tasks merely waking on timeout.

`WFI` is used during idle time. STOP mode is not claimed because continuous
100 kS/s conversion is incompatible with stopping the clock tree.

## Status lifetime

Signal-quality flags are calculated from each completed result and recover on
the next valid result. Acquisition gaps, overruns, stale DMA ownership, dropped
frames, UART backpressure, reset cause, and explicit fault injection are sticky
until reset. This split lets the display recover immediately after reconnecting
a signal while preserving intermittent infrastructure failures for diagnosis.

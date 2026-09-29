# Real-time, memory, and bandwidth budget

The task deadlines and stack sizes are design limits, not all measured task
WCETs. During the 2026-09-28 DAC-to-dual-ADC loopback endurance run of
1,800.071 s, the firmware's DWT-derived DSP processing field had a maximum
of 1,742 us and p99.9 of 1,731 us, with zero samples reaching the 9 ms
deadline. The observed maximum leaves 7,258 us of margin against that design
limit, but is not a proven worst-case bound. Task stack margins and an
independent GPIO timing trace remain unmeasured. See the
[run evidence](../evidence/run-20260928/additional-validation.md).

| Task | Priority | Activation | Deadline/WCET target | Stack |
|---|---:|---:|---:|---:|
| watchdog | 6 | 250 ms | 50 us | 256 words |
| acquisition | 5 | 10.24 ms | 300 us | 384 words |
| DSP | 4 | each block | 9.0 ms | 768 words |
| telemetry | 3 | 20 Hz | 45 ms elapsed, DMA-blocked | 512 words |

Large FFT/result/frame objects are static task-owned buffers, not automatic
stack variables. Approximate static memory before HAL/FreeRTOS is: ADC DMA
8 KiB, DAC LUT 4 KiB, deinterleaved samples 4 KiB, two result objects about
5 KiB, result queue about 2.5 KiB, protocol frame 474 B, plus the selected FFT
backend. The CMSIS target workspace omits the portable float arrays.

The half-buffer arrives every 10.24 ms. DSP must complete in 9 ms, leaving
1.24 ms for scheduling and interrupt jitter before DMA can overwrite that half.
Telemetry publishes only every 50 ms and blocks on DMA completion, so UART wire
time is not counted as CPU execution.

At 921600 baud 8-N-1, usable throughput is 92,160 bytes/s. Eight maximum-size
474-byte packets at 20 Hz require 75,840 bytes/s, leaving about 17.7% line-rate
margin. Actual frames are normally shorter because only reserved delimiter
bytes are escaped.

The DWT-derived maximum and p99.9 above satisfy the observed processing-time
check, and zero dropped acquisition blocks were reported over 30 minutes.
GPIO pulse-width validation and the target of at least 25% task stack headroom
remain open.

TIM5 is configured as the application timestamp source at 1 MHz, giving a
71.58-minute unsigned wrap period. DWT remains available for short-interval
cycle profiling only; it is not used as the long-running timestamp. The DSP
task records `DWT->CYCCNT` before DC removal and after both channel FFTs, then
converts the wrap-safe cycle delta to `processing_us` for every telemetry frame.
The 9 ms value is a design limit; the 30-minute observed maximum is below it.

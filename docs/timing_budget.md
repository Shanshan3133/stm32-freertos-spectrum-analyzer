# Real-time, memory, and bandwidth budget

The task deadlines and stack sizes are design limits. A short on-board UART
smoke run observed a maximum 1,812 us in the firmware's DWT-derived DSP
processing field over 60 s; this is not an established WCET or 9 ms deadline
acceptance result. The loopback input, long-run tail, stack margins, and
independent timing trace still need validation. With the DAC-to-dual-ADC
loopback connected on 2026-09-28, the maximum `processing_us` reported over
60 s was 1,752 us. This is an observed short-run maximum, not WCET.

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

During the loopback acceptance run, measure DWT cycles and GPIO pulse widths,
then report maximum and p99.9 results. Require 25% stack headroom and zero
dropped acquisition blocks during a 30-minute stress run.

TIM5 is configured as the application timestamp source at 1 MHz, giving a
71.58-minute unsigned wrap period. DWT remains available for short-interval
cycle profiling only; it is not used as the long-running timestamp. The DSP
task records `DWT->CYCCNT` before DC removal and after both channel FFTs, then
converts the wrap-safe cycle delta to `processing_us` for every telemetry frame.
The 9 ms value remains an acceptance limit until real hardware results replace
the `TBD` fields.

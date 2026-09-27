# FreeRTOS Dual-Channel Real-Time Spectrum Analyzer

[![Host verification](https://github.com/Shanshan3133/stm32-freertos-spectrum-analyzer/actions/workflows/ci.yml/badge.svg)](https://github.com/Shanshan3133/stm32-freertos-spectrum-analyzer/actions/workflows/ci.yml)

Portfolio firmware for the STM32F446RE. The design samples two analog channels
simultaneously at 100 kS/s per channel, processes 1024-sample blocks, and emits
RMS, peak, dominant-frequency, waveform-preview, and spectrum data at 20 Hz.

The minimum hardware is one NUCLEO-F446RE, its Mini-USB data cable, and two
male-to-male jumper wires. The STM32 DAC generates a coherent 976.5625 Hz
self-test tone; wiring PA4 to PA0 and PA1 closes the complete DAC-to-ADC loop.
No sensor module, soldering, external programmer, or USB-to-UART adapter is
required. A logic analyzer is useful evidence but is not required to run it.

## Honest completion status

| Status | Scope |
|---|---|
| Host verified | CRC-16, framing, recovery, bounded spectrum reassembly, complete CSV export, bandwidth bound, and 21 Python/fault-injection tests; portable FFT/C tests pass Cortex-M4 strict compile checks and execute in Linux CI |
| Built and flashed on NUCLEO-F446RE | Reproducible ARM-GCC target build with official STM32CubeF4 HAL, FreeRTOS and CMSIS-DSP; dual-ADC/DAC DMA pipeline, USART2 DMA, task health voting, IWDG policy and WFI idle |
| Initial board smoke test | Without the two loopback wires, a 60 s ST-LINK virtual COM run produced 1194 channel-0 and 1193 channel-1 complete spectra, zero CRC/format/sequence errors and zero reported dropped blocks; maximum observed DSP processing field was 1,812 us |
| Watchdog recovery observed | A dedicated DSP-stall firmware build caused repeated IWDG resets and reported `STATUS_WATCHDOG_RESET`; the normal firmware was restored and rechecked |
| Still requires loopback and longer measurements | Actual DAC-to-ADC frequency accuracy, independently measured 100 kS/s trigger timing, sustained WCET/stack headroom, 30-minute endurance and external captures |

The 60 s smoke result is real board telemetry, not proof of ADC sample-rate
accuracy or a worst-case deadline. Its no-wire inputs are floating. See the
[dated evidence](evidence/run-20260927/metadata.md) and
[hardware acceptance checklist](docs/validation.md) for details.

## Architecture

- ADC1 and ADC2 use dual regular simultaneous mode, triggered by TIM2 at
  100 kHz. DMA stores packed 12-bit samples in a two-half circular buffer.
- The acquisition task is released by DMA half/full-complete notifications and
  checks timestamp/generation continuity. The DSP task rejects a descriptor if
  DMA begins reusing its half-buffer before the samples are copied.
- The DSP task applies a Hann window and 1024-point FFT independently to both
  channels, calculates RMS/peak/dominant frequency, and overwrites a one-entry
  latest-result queue.
- The telemetry task compresses Q15 magnitudes to 8-bit values and sends eight
  bounded packets per result using USART2 TX DMA through the board's ST-LINK
  virtual COM port at 921600 baud.
- TIM5 runs as a 1 MHz 32-bit timebase, avoiding the approximately 23.9-second
  wrap that a raw 180 MHz DWT counter would have caused.
- The watchdog task feeds IWDG only after acquisition, DSP, and telemetry have
  all reported real data-path progress within the voting window. Queue or DMA
  timeouts deliberately withhold a vote instead of disguising a stalled pipeline.
- Idle uses `WFI`; deeper STOP-mode claims are deliberately outside this
  continuous 100 kS/s instrument.

See [architecture](docs/architecture.md), [protocol](docs/protocol.md), and the
[timing budget](docs/timing_budget.md).

The processing order is deliberately fixed:

```text
unpack ADC -> calculate/remove each block's DC mean -> Hann window
-> 1024-point FFT -> magnitude and peak interpolation
```

Removing DC before applying the window avoids converting a DC offset into
window-shaped spectral leakage. A float-versus-Q15 model comparison is
published in [FFT precision](docs/fft_precision.md).

## Status recovery policy

```mermaid
stateDiagram-v2
    [*] --> Starting
    Starting --> Valid: current block passes both channels
    Valid --> SignalFault: weak / clipped / frozen / numeric
    SignalFault --> Valid: next complete block passes both channels
    Valid --> LatchedFault: DMA gap / stale buffer / UART timeout / dropped frame
    SignalFault --> LatchedFault: infrastructure fault
    LatchedFault --> Starting: MCU reset
    LatchedFault --> WatchdogReset: missing task health vote
    WatchdogReset --> Starting: reboot; reset-cause bit retained in telemetry
```

Signal-quality bits describe the current FFT result and therefore clear on the
next valid result. Infrastructure faults are sticky evidence and clear only on
MCU reset. `STATUS_WATCHDOG_RESET` reports the previous boot cause; the hardware
reset flags are cleared after capture. This intentionally favors diagnosability
over silently hiding an intermittent DMA or transport failure.

## Repository map

```text
app/                 FreeRTOS application tasks
core/                Portable FFT reference, health checks, protocol
include/             Shared interfaces and data structures
platform/stm32f446/  HAL/DMA adapter and CMSIS-DSP target backend
tools/               Python decoder, CSV logger, live plotter
tests/               C behavior tests and Python fault-injection tests
docs/                Wiring, architecture, timing, and validation
```

## Verify without hardware

```powershell
python -m unittest discover -s tests -v
python tools\spectrum_monitor.py --self-test
python tools\fft_precision_report.py --check
powershell -ExecutionPolicy Bypass -File tools\verify.ps1
```

The PowerShell script uses the ARM GCC shipped under `C:\ST`, compiles the
portable C modules and C test source with warnings as errors, and runs the
Python suite. This is a local cross-compile check, not execution of the ARM
objects. GitHub Actions additionally builds and executes the portable C tests
with CTest on every push and pull request.

For live serial input, plotting, and the precision report, install the
host-only packages:

```powershell
python -m pip install -r requirements.txt
```

## Build and run on the board

On Windows with STM32CubeIDE, obtain the pinned official SDK and build the
standalone CMake target:

```powershell
powershell -ExecutionPolicy Bypass -File tools\fetch_sdk.ps1
powershell -ExecutionPolicy Bypass -File tools\build_target.ps1
powershell -ExecutionPolicy Bypass -File tools\flash_target.ps1
```

The binary is `build/target-stm32f446/spectrum_f446.bin`; the flash helper
programs it at `0x08000000` and verifies the read-back. The tested board is an ST-LINK V2.1
NUCLEO-F446RE, using its virtual COM port at 921600 baud. Run the bounded
no-wire serial check before connecting the loopback:

```powershell
python tools\serial_smoke.py --port COM6 --seconds 10
```

Then power off and connect:

```text
PA4 / A2 (DAC output) -> PA0 / A0 (ADC channel 1)
PA4 / A2 (DAC output) -> PA1 / A1 (ADC channel 2)
```

Power on and open the ST-LINK virtual COM port at 921600 8-N-1, then run:

```powershell
python tools\spectrum_monitor.py --port COM6 --baud 921600 --plot `
  --csv summary.csv --spectrum-csv spectrum.csv --waveform-csv waveform.csv
```

The expected built-in tone is FFT bin 10:
`100000 * 10 / 1024 = 976.5625 Hz`. Replace `COM6` with the enumerated port.

Dedicated validation builds can inject acquisition drops, a frozen ADC input,
UART failures, or a DSP deadlock. See [fault injection](docs/fault_injection.md).

## Resume wording

Use these bullets only after the hardware acceptance sheet passes:

- Developed a dual-channel real-time spectrum analyzer on STM32F446 using
  timer-triggered simultaneous ADC sampling, DMA ping-pong buffering, and a
  CMSIS-DSP 1024-point Q15 FFT at 100 kS/s per channel.
- Designed a priority-scheduled FreeRTOS acquisition/DSP/telemetry pipeline and
  streamed bounded CRC-protected binary frames over USART DMA at 20 Hz.
- Validated frequency accuracy, end-to-end latency, task stack margin, watchdog
  recovery, and protocol fault handling using DAC loopback, GPIO timing traces,
  and automated Python tests.

Before hardware validation, use the accurate application-ready wording in
[resume and interview notes](docs/resume_and_interview.md). Do not claim a
measured target sampling rate, WCET, or endurance result until the acceptance
sheet contains the corresponding evidence.

## License

MIT.

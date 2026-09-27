# NUCLEO-F446RE target build

The repository now contains a standalone STM32F446RE target under
`target/stm32f446/`. Run `tools/fetch_sdk.ps1` once to check out pinned official
STM32CubeF4 sources, then `tools/build_target.ps1` to build an ARM binary with
the compiler, CMake and Ninja bundled in STM32CubeIDE 2.2. The firmware was
flashed and produced valid dual-channel telemetry through the ST-LINK VCP
without loopback wires. The loopback and full hardware acceptance are pending.

The settings below also document equivalent manual CubeMX/IDE configuration;
they are not required for the standalone build.

Create a new STM32 project for `NUCLEO-F446RE`. Generate HAL and FreeRTOS code,
then add `app/`, `core/health_monitor.c`, `core/telemetry.c`,
`platform/stm32f446/board_skeleton.c`, and
`platform/stm32f446/spectrum_cmsis.c`. Do not compile `core/spectrum.c` in the
target build. Add `include/` and CMSIS-DSP include paths and define:

```text
ANALYZER_STM32F446
ANALYZER_USE_CMSIS_DSP
ARM_MATH_CM4
```

Use hard-float Cortex-M4 options and link CMSIS-DSP plus `libm`.

## Peripheral configuration

| Resource | Required setting |
|---|---|
| system clock | 180 MHz, valid APB prescalers, FPU enabled |
| TIM2 | update/TRGO at exactly 100 kHz; master output trigger = update |
| TIM5 | 32-bit up-counter at 1 MHz, period `0xFFFFFFFF`; no interrupt |
| ADC1 | IN0/PA0, 12 bit, external trigger TIM2 TRGO, rising edge |
| ADC2 | IN1/PA1, same trigger and sample time as ADC1 |
| ADC multimode | dual regular simultaneous; DMA access mode for packed CDR; continuous DMA requests |
| ADC DMA | circular, peripheral word, memory word, 2048 words, half/full IRQ |
| DAC1 | OUT1/PA4, trigger TIM2 TRGO, output buffer enabled |
| DAC DMA | circular, memory word, peripheral word, 1024 entries |
| USART2 | asynchronous 921600, 8-N-1, TX PA2 routed to ST-LINK VCP |
| USART2 TX DMA | DMA1 Stream 6, Channel 4, normal mode |
| IWDG | approximately 1 s timeout; exact value depends on measured LSI |

Use ADC sampling time sufficient for the board's source impedance; 15 cycles or
longer is a safe starting point for the DAC loopback. Verify conversion time is
comfortably below the 10 us trigger interval.

With a 90 MHz APB2 timer clock, select ADC prescaler `/4` for a 22.5 MHz ADC
clock. With a 90 MHz TIM5 input clock, use prescaler 89 for the 1 MHz timebase.
Confirm the actual timer clock after applying the APB multiplier rules.

DMA/UART interrupt priorities that call FreeRTOS `FromISR` APIs must be
numerically equal to or greater than
`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`. Enable `configASSERT`, stack
overflow hook, malloc-failed hook, and idle hook. A 1 kHz RTOS tick is adequate.

## Important board fact

The NUCLEO-F446RE's onboard USB connector belongs to ST-LINK. The target MCU
does not have an onboard user USB connector, so this minimum build does not
claim native target USB CDC. USART2 reaches the PC through the ST-LINK virtual
COM bridge using the same Mini-USB cable.

## First run

1. Flash `build/target-stm32f446/spectrum_f446.bin` at `0x08000000` and run
   `python tools/serial_smoke.py --port COM6 --seconds 10` (adjust port).
2. Connect PA4/A2 to PA0/A0 and PA1/A1 with power off.
3. Power on, start the Python monitor at 921600 baud, and confirm 976.5625 Hz.
4. Run the remaining acceptance checklist and record evidence. IWDG is enabled
   in the tested build; its deliberate reset recovery has not yet been tested.

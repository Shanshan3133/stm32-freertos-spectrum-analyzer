#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define ADC_SAMPLE_RATE_HZ          100000u
#define ADC_CHANNEL_COUNT                2u
#define ADC_DMA_FRAMES_PER_HALF        1024u
#define SPECTRUM_OUTPUT_HZ               20u
#define SPECTRUM_QUEUE_DEPTH              1u

#define TASK_PRIORITY_WATCHDOG            6u
#define TASK_PRIORITY_ACQUISITION         5u
#define TASK_PRIORITY_DSP                 4u
#define TASK_PRIORITY_TELEMETRY           3u
#define TASK_PRIORITY_MONITOR             2u

#define DSP_DEADLINE_US                9000u
#define ADC_BLOCK_TIMEOUT_MS              20u
#define UART_BAUD_RATE                 921600u
#define UART_FRAME_TIMEOUT_MS              8u
#define ENABLE_DAC_LOOPBACK_TEST            1u
#define WATCHDOG_WINDOW_MS               250u
#define WATCHDOG_TIMEOUT_MS             1000u

/* Set one option nonzero only in a dedicated validation build. */
#define FAULT_INJECT_DROP_EVERY_N_BLOCKS     0u
#ifndef FAULT_INJECT_DSP_STALL_AFTER_BLOCKS
#define FAULT_INJECT_DSP_STALL_AFTER_BLOCKS  0u
#endif
#define FAULT_INJECT_UART_FAIL_EVERY_N_FRAMES 0u
#ifndef FAULT_INJECT_FREEZE_ADC
#define FAULT_INJECT_FREEZE_ADC              0u
#endif

#endif

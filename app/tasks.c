#include "app.h"

#include "analyzer_types.h"
#include "app_config.h"
#include "health_monitor.h"
#include "platform.h"
#include "spectrum.h"
#include "telemetry.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <string.h>

static QueueHandle_t block_queue;
static QueueHandle_t result_queue;
static StaticQueue_t block_queue_control;
static StaticQueue_t result_queue_control;
static uint8_t block_queue_storage[2u * sizeof(adc_dma_block_t)];
static uint8_t result_queue_storage[sizeof(spectrum_result_t)];

static spectrum_workspace_t spectrum_workspace[ANALYZER_CHANNELS];
static int16_t channel_samples[ANALYZER_CHANNELS][ANALYZER_FFT_SIZE];
static uint16_t channel_clipped_samples[ANALYZER_CHANNELS];
/* Task-owned static buffers keep large FFT results off the FreeRTOS stacks. */
static spectrum_result_t dsp_result;
static spectrum_result_t telemetry_result;
static uint8_t telemetry_frame[TELEMETRY_MAX_FRAME];
static volatile uint32_t health_mask;
static volatile uint32_t sticky_status;

static void status_set(uint32_t bits) {
    taskENTER_CRITICAL();
    sticky_status |= bits;
    taskEXIT_CRITICAL();
}

static uint32_t status_get(void) {
    taskENTER_CRITICAL();
    const uint32_t value = sticky_status;
    taskEXIT_CRITICAL();
    return value;
}

void app_health_kick(uint32_t task_bit) {
    taskENTER_CRITICAL();
    health_mask |= task_bit;
    taskEXIT_CRITICAL();
}

static void acquisition_task(void *argument) {
    (void)argument;
    acquisition_health_t monitor;
    acquisition_health_init(&monitor);
    configASSERT(platform_adc_start());
    if (platform_test_signal_active()) status_set(STATUS_TEST_SIGNAL);
    const uint32_t block_period_us =
        (ADC_DMA_FRAMES_PER_HALF * 1000000u) / ADC_SAMPLE_RATE_HZ;
#if FAULT_INJECT_DROP_EVERY_N_BLOCKS > 0u
    uint32_t blocks_seen = 0u;
#endif

    for (;;) {
        adc_dma_block_t block;
        if (!platform_adc_wait_block(&block, ADC_BLOCK_TIMEOUT_MS)) {
            status_set(STATUS_ADC_OVERRUN);
            /* A timeout is not progress. Withhold the watchdog vote so a
             * stalled ADC/DMA path is recovered by IWDG. */
            continue;
        }
        status_set(acquisition_health_check(&monitor, &block,
                                            block_period_us));
        if (platform_adc_overruns() != 0u) status_set(STATUS_ADC_OVERRUN);
#if FAULT_INJECT_DROP_EVERY_N_BLOCKS > 0u
        ++blocks_seen;
        if ((blocks_seen % FAULT_INJECT_DROP_EVERY_N_BLOCKS) == 0u) {
            status_set(STATUS_FRAME_DROPPED | STATUS_FAULT_INJECTED);
            app_health_kick(HEALTH_ACQUISITION);
            continue;
        }
#endif
        if (xQueueSend(block_queue, &block, 0u) != pdPASS) {
            status_set(STATUS_FRAME_DROPPED);
        }
        app_health_kick(HEALTH_ACQUISITION);
    }
}

static void unpack_dual_adc(const adc_dma_block_t *block) {
    uint32_t sums[ANALYZER_CHANNELS] = {0u};
    memset(channel_clipped_samples, 0, sizeof(channel_clipped_samples));
    for (size_t i = 0u; i < ANALYZER_FFT_SIZE; ++i) {
#if FAULT_INJECT_FREEZE_ADC
        const uint32_t packed = block->packed_samples[0];
#else
        const uint32_t packed = block->packed_samples[i];
#endif
        const uint16_t adc1 = (uint16_t)(packed & 0x0FFFu);
        const uint16_t adc2 = (uint16_t)((packed >> 16) & 0x0FFFu);
        sums[0] += adc1;
        sums[1] += adc2;
        if (adc1 <= 4u || adc1 >= 4091u) ++channel_clipped_samples[0];
        if (adc2 <= 4u || adc2 >= 4091u) ++channel_clipped_samples[1];
    }
    const int32_t means[ANALYZER_CHANNELS] = {
        (int32_t)((sums[0] + ANALYZER_FFT_SIZE / 2u) / ANALYZER_FFT_SIZE),
        (int32_t)((sums[1] + ANALYZER_FFT_SIZE / 2u) / ANALYZER_FFT_SIZE)
    };
    for (size_t i = 0u; i < ANALYZER_FFT_SIZE; ++i) {
#if FAULT_INJECT_FREEZE_ADC
        const uint32_t packed = block->packed_samples[0];
#else
        const uint32_t packed = block->packed_samples[i];
#endif
        const int32_t adc1 = (int32_t)(packed & 0x0FFFu) - means[0];
        const int32_t adc2 =
            (int32_t)((packed >> 16) & 0x0FFFu) - means[1];
        channel_samples[0][i] = (int16_t)(adc1 << 4);
        channel_samples[1][i] = (int16_t)(adc2 << 4);
    }
}

static void dsp_task(void *argument) {
    (void)argument;
    for (unsigned channel = 0u; channel < ANALYZER_CHANNELS; ++channel) {
        spectrum_workspace_init(&spectrum_workspace[channel]);
    }
#if FAULT_INJECT_DSP_STALL_AFTER_BLOCKS > 0u
    uint32_t blocks_processed = 0u;
#endif
    for (;;) {
        adc_dma_block_t block;
        if (xQueueReceive(block_queue, &block,
                          pdMS_TO_TICKS(ADC_BLOCK_TIMEOUT_MS)) != pdPASS) {
            /* Do not hide an upstream acquisition stall from the watchdog. */
            continue;
        }
        memset(&dsp_result, 0, sizeof(dsp_result));
        dsp_result.timestamp_us = block.timestamp_us;
        dsp_result.generation = block.generation;
        dsp_result.sample_rate_hz = ADC_SAMPLE_RATE_HZ;
        const uint32_t started_cycles = platform_cycle_count();
        if (platform_adc_generation() != block.generation) {
            status_set(STATUS_DMA_STALE | STATUS_FRAME_DROPPED);
            app_health_kick(HEALTH_DSP);
            continue;
        }
        unpack_dual_adc(&block);
        if (platform_adc_generation() != block.generation) {
            status_set(STATUS_DMA_STALE | STATUS_FRAME_DROPPED);
            app_health_kick(HEALTH_DSP);
            continue;
        }
        for (unsigned channel = 0u; channel < ANALYZER_CHANNELS; ++channel) {
            if (!spectrum_analyze_q15(&spectrum_workspace[channel],
                                      channel_samples[channel],
                                      ADC_SAMPLE_RATE_HZ,
                                      &dsp_result.channel[channel])) {
                dsp_result.status |= STATUS_NUMERIC_FAULT;
            }
            dsp_result.channel[channel].clipped_samples =
                channel_clipped_samples[channel];
            for (size_t i = 0u; i < ANALYZER_PREVIEW_SAMPLES; ++i) {
                dsp_result.channel[channel].preview_q15[i] =
                    channel_samples[channel][i *
                        (ANALYZER_FFT_SIZE / ANALYZER_PREVIEW_SAMPLES)];
            }
        }
        const uint32_t elapsed_cycles =
            platform_cycle_count() - started_cycles;
        dsp_result.processing_us = elapsed_cycles /
            (platform_core_clock_hz() / 1000000u);
        dsp_result.dropped_blocks = platform_adc_overruns();
        dsp_result.status |= status_get();
        dsp_result.status |= spectrum_health_check(&dsp_result,
                                                   DSP_DEADLINE_US);
        (void)xQueueOverwrite(result_queue, &dsp_result);
#if FAULT_INJECT_DSP_STALL_AFTER_BLOCKS > 0u
        ++blocks_processed;
        if (blocks_processed >= FAULT_INJECT_DSP_STALL_AFTER_BLOCKS) {
            status_set(STATUS_FAULT_INJECTED);
            vTaskSuspend(NULL);
        }
#endif
        app_health_kick(HEALTH_DSP);
    }
}

static void telemetry_task(void *argument) {
    (void)argument;
    uint16_t sequence = 0u;
    TickType_t last_output = 0u;
#if FAULT_INJECT_UART_FAIL_EVERY_N_FRAMES > 0u
    uint32_t frames_attempted = 0u;
#endif
    for (;;) {
        if (xQueueReceive(result_queue, &telemetry_result,
                          pdMS_TO_TICKS(100u)) != pdPASS) {
            /* A missing result means the pipeline has not progressed. */
            continue;
        }
        const TickType_t now = xTaskGetTickCount();
        if ((now - last_output) < pdMS_TO_TICKS(1000u / SPECTRUM_OUTPUT_HZ)) {
            app_health_kick(HEALTH_TELEMETRY);
            continue;
        }
        for (uint8_t channel = 0u; channel < ANALYZER_CHANNELS; ++channel) {
            for (uint8_t chunk = 0u;
                 chunk < ANALYZER_CHUNKS_PER_CHANNEL; ++chunk) {
                const size_t length = telemetry_encode_spectrum_chunk(
                    sequence, &telemetry_result, channel, chunk,
                    telemetry_frame, sizeof(telemetry_frame));
#if FAULT_INJECT_UART_FAIL_EVERY_N_FRAMES > 0u
                ++frames_attempted;
                const bool injected_failure =
                    (frames_attempted %
                     FAULT_INJECT_UART_FAIL_EVERY_N_FRAMES) == 0u;
                if (injected_failure) status_set(STATUS_FAULT_INJECTED);
#else
                const bool injected_failure = false;
#endif
                if (length == 0u || injected_failure ||
                    !platform_uart_write_dma(telemetry_frame, length,
                                             UART_FRAME_TIMEOUT_MS)) {
                    status_set(STATUS_UART_BACKPRESSURE);
                    break;
                }
                ++sequence;
            }
        }
        last_output = now;
        app_health_kick(HEALTH_TELEMETRY);
    }
}

static void watchdog_task(void *argument) {
    (void)argument;
    platform_watchdog_start(WATCHDOG_TIMEOUT_MS);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_WINDOW_MS));
        taskENTER_CRITICAL();
        const uint32_t observed = health_mask;
        health_mask = 0u;
        taskEXIT_CRITICAL();
        if ((observed & HEALTH_REQUIRED) == HEALTH_REQUIRED) {
            platform_watchdog_feed();
        }
    }
}

void app_start(void) {
    if (platform_watchdog_reset_detected()) {
        status_set(STATUS_WATCHDOG_RESET);
    }
    block_queue = xQueueCreateStatic(2u, sizeof(adc_dma_block_t),
                                     block_queue_storage,
                                     &block_queue_control);
    result_queue = xQueueCreateStatic(SPECTRUM_QUEUE_DEPTH,
                                      sizeof(spectrum_result_t),
                                      result_queue_storage,
                                      &result_queue_control);
    configASSERT(block_queue != NULL && result_queue != NULL);
    configASSERT(xTaskCreate(acquisition_task, "adc", 384u, NULL,
                             TASK_PRIORITY_ACQUISITION, NULL) == pdPASS);
    configASSERT(xTaskCreate(dsp_task, "dsp", 768u, NULL,
                             TASK_PRIORITY_DSP, NULL) == pdPASS);
    configASSERT(xTaskCreate(telemetry_task, "uart", 512u, NULL,
                             TASK_PRIORITY_TELEMETRY, NULL) == pdPASS);
    configASSERT(xTaskCreate(watchdog_task, "wdg", 256u, NULL,
                             TASK_PRIORITY_WATCHDOG, NULL) == pdPASS);
}

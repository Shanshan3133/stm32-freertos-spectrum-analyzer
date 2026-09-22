#include "analyzer_types.h"
#include "health_monitor.h"
#include "spectrum.h"
#include "telemetry.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_D 3.14159265358979323846

static void make_tone(int16_t *samples, uint32_t sample_rate_hz,
                      float frequency_hz, float amplitude) {
    for (size_t i = 0u; i < ANALYZER_FFT_SIZE; ++i) {
        const double phase = 2.0 * PI_D * frequency_hz * (double)i /
                             (double)sample_rate_hz;
        samples[i] = (int16_t)lround(sin(phase) * amplitude * 32767.0);
    }
}

static void test_crc(void) {
    static const uint8_t vector[] = "123456789";
    assert(crc16_ccitt_false(vector, sizeof(vector) - 1u) == 0x29B1u);
}

static void test_exact_bin_spectrum(void) {
    static spectrum_workspace_t workspace;
    static int16_t samples[ANALYZER_FFT_SIZE];
    channel_spectrum_t result;
    make_tone(samples, 102400u, 1000.0f, 0.8f);
    spectrum_workspace_init(&workspace);
    assert(spectrum_analyze_q15(&workspace, samples, 102400u, &result));
    assert(result.dominant_millihz > 995000u);
    assert(result.dominant_millihz < 1005000u);
    const float rms = result.rms_q15 / 32768.0f;
    assert(rms > 0.560f && rms < 0.570f);
    assert(result.magnitude_q15[10] > 25000u);
}

static void test_off_bin_interpolation(void) {
    static spectrum_workspace_t workspace;
    static int16_t samples[ANALYZER_FFT_SIZE];
    channel_spectrum_t result;
    make_tone(samples, 100000u, 440.0f, 0.6f);
    assert(spectrum_analyze_q15(&workspace, samples, 100000u, &result));
    assert(result.dominant_millihz > 425000u);
    assert(result.dominant_millihz < 455000u);
}

static void fill_result(spectrum_result_t *result) {
    memset(result, 0, sizeof(*result));
    result->timestamp_us = 123456u;
    result->generation = 9u;
    result->sample_rate_hz = 100000u;
    result->processing_us = 2300u;
    result->dropped_blocks = 2u;
    result->status = STATUS_ADC_RUNNING | STATUS_SIGNAL_VALID;
    result->channel[1].rms_q15 = 12000u;
    result->channel[1].peak_q15 = 20000u;
    result->channel[1].dominant_millihz = 4000000u;
    for (size_t i = 0u; i < ANALYZER_SPECTRUM_BINS; ++i) {
        result->channel[1].magnitude_q15[i] = (uint16_t)(i * 17u);
    }
    for (size_t i = 0u; i < ANALYZER_PREVIEW_SAMPLES; ++i) {
        result->channel[1].preview_q15[i] = (int16_t)(i * 101 - 6000);
    }
}

static void test_telemetry_round_trip(void) {
    spectrum_result_t result;
    fill_result(&result);
    uint8_t encoded[TELEMETRY_MAX_FRAME];
    const size_t length = telemetry_encode_spectrum_chunk(
        65535u, &result, 1u, 2u, encoded, sizeof(encoded));
    assert(length > TELEMETRY_RAW_SPECTRUM_LEN);

    telemetry_decoder_t decoder;
    spectrum_chunk_t chunk;
    telemetry_decoder_init(&decoder);
    telemetry_decode_result_t decoded = TELEMETRY_DECODE_NONE;
    for (size_t i = 0u; i < length; ++i) {
        const telemetry_decode_result_t current = telemetry_decoder_feed(
            &decoder, encoded[i], &chunk);
        if (current == TELEMETRY_DECODE_FRAME) decoded = current;
    }
    assert(decoded == TELEMETRY_DECODE_FRAME);
    assert(chunk.sequence == 65535u);
    assert(chunk.channel == 1u && chunk.chunk_index == 2u);
    assert(chunk.bin_start == 256u && chunk.bin_count == 128u);
    assert(chunk.dominant_millihz == 4000000u);
    assert(chunk.magnitude_u8[3] ==
           (uint8_t)((result.channel[1].magnitude_q15[259] + 64u) >> 7));
    assert(chunk.preview_q15[3] == result.channel[1].preview_q15[67]);
    assert(decoder.stats.frames_ok == 1u);
}

static void test_telemetry_magnitude_saturates(void) {
    spectrum_result_t result;
    fill_result(&result);
    result.channel[0].magnitude_q15[0] = 32767u;
    uint8_t encoded[TELEMETRY_MAX_FRAME];
    const size_t length = telemetry_encode_spectrum_chunk(
        1u, &result, 0u, 0u, encoded, sizeof(encoded));
    telemetry_decoder_t decoder;
    spectrum_chunk_t chunk;
    telemetry_decoder_init(&decoder);
    for (size_t i = 0u; i < length; ++i) {
        (void)telemetry_decoder_feed(&decoder, encoded[i], &chunk);
    }
    assert(chunk.magnitude_u8[0] == 255u);
}

static void test_decoder_recovers_after_corruption(void) {
    spectrum_result_t result;
    fill_result(&result);
    uint8_t frame[TELEMETRY_MAX_FRAME];
    size_t length = telemetry_encode_spectrum_chunk(
        1u, &result, 0u, 0u, frame, sizeof(frame));
    assert(length != 0u);
    for (size_t i = 8u; i + 1u < length; ++i) {
        if (frame[i] != TELEMETRY_SOF && frame[i] != TELEMETRY_ESC &&
            (uint8_t)(frame[i] ^ 1u) != TELEMETRY_SOF &&
            (uint8_t)(frame[i] ^ 1u) != TELEMETRY_ESC) {
            frame[i] ^= 1u;
            break;
        }
    }
    telemetry_decoder_t decoder;
    spectrum_chunk_t chunk;
    telemetry_decoder_init(&decoder);
    for (size_t i = 0u; i < length; ++i) {
        (void)telemetry_decoder_feed(&decoder, frame[i], &chunk);
    }
    assert(decoder.stats.crc_errors == 1u);
    length = telemetry_encode_spectrum_chunk(
        2u, &result, 0u, 0u, frame, sizeof(frame));
    telemetry_decode_result_t final = TELEMETRY_DECODE_NONE;
    for (size_t i = 0u; i < length; ++i) {
        final = telemetry_decoder_feed(&decoder, frame[i], &chunk);
    }
    assert(final == TELEMETRY_DECODE_FRAME);
    assert(chunk.sequence == 2u);
}

static void feed_frame(telemetry_decoder_t *decoder,
                       const uint8_t *frame, size_t length,
                       spectrum_chunk_t *chunk) {
    for (size_t i = 0u; i < length; ++i) {
        (void)telemetry_decoder_feed(decoder, frame[i], chunk);
    }
}

static void test_sequence_classification(void) {
    spectrum_result_t result;
    fill_result(&result);
    telemetry_decoder_t decoder;
    spectrum_chunk_t chunk;
    uint8_t frame[TELEMETRY_MAX_FRAME];
    telemetry_decoder_init(&decoder);

    size_t length = telemetry_encode_spectrum_chunk(
        10u, &result, 0u, 0u, frame, sizeof(frame));
    feed_frame(&decoder, frame, length, &chunk);
    length = telemetry_encode_spectrum_chunk(
        10u, &result, 0u, 0u, frame, sizeof(frame));
    feed_frame(&decoder, frame, length, &chunk);
    length = telemetry_encode_spectrum_chunk(
        9u, &result, 0u, 0u, frame, sizeof(frame));
    feed_frame(&decoder, frame, length, &chunk);
    length = telemetry_encode_spectrum_chunk(
        12u, &result, 0u, 0u, frame, sizeof(frame));
    feed_frame(&decoder, frame, length, &chunk);

    assert(decoder.stats.sequence_duplicates == 1u);
    assert(decoder.stats.sequence_out_of_order == 1u);
    assert(decoder.stats.sequence_lost == 1u);
}

static void test_acquisition_health(void) {
    uint32_t samples[ANALYZER_FFT_SIZE] = {0u};
    adc_dma_block_t block = {
        .packed_samples = samples,
        .timestamp_us = 10000u,
        .generation = 1u,
        .frames = ANALYZER_FFT_SIZE
    };
    acquisition_health_t monitor;
    acquisition_health_init(&monitor);
    assert(acquisition_health_check(&monitor, &block, 10240u) ==
           STATUS_ADC_RUNNING);
    block.generation = 3u;
    block.timestamp_us += 20480u;
    const uint32_t status = acquisition_health_check(&monitor, &block, 10240u);
    assert((status & STATUS_BLOCK_GAP) != 0u);
    assert(monitor.dropped_blocks == 1u);
}

static void test_spectrum_health(void) {
    spectrum_result_t result;
    fill_result(&result);
    result.processing_us = 9001u;
    assert((spectrum_health_check(&result, 9000u) & STATUS_DSP_DEADLINE) != 0u);
    result.channel[0].dominant_millihz = 51000000u;
    assert((spectrum_health_check(&result, 9000u) & STATUS_NUMERIC_FAULT) != 0u);
}

static void test_signal_quality_flags(void) {
    spectrum_result_t result;
    memset(&result, 0, sizeof(result));
    result.sample_rate_hz = 100000u;
    for (unsigned channel = 0u; channel < ANALYZER_CHANNELS; ++channel) {
        result.channel[channel].rms_q15 = 1000u;
        result.channel[channel].peak_q15 = 2000u;
        for (size_t i = 0u; i < ANALYZER_PREVIEW_SAMPLES; ++i) {
            result.channel[channel].preview_q15[i] =
                (i & 1u) != 0u ? 1000 : -1000;
        }
    }
    uint32_t status = spectrum_health_check(&result, 9000u);
    assert((status & STATUS_SIGNAL_VALID) != 0u);
    result.channel[0].rms_q15 = 0u;
    status = spectrum_health_check(&result, 9000u);
    assert((status & STATUS_SIGNAL_WEAK) != 0u);
    assert((status & STATUS_SIGNAL_VALID) == 0u);
    result.channel[0].rms_q15 = 1000u;
    result.channel[0].peak_q15 = 32767u;
    status = spectrum_health_check(&result, 9000u);
    assert((status & STATUS_ADC_CLIPPING) != 0u);
    result.channel[0].peak_q15 = 2000u;
    memset(result.channel[0].preview_q15, 0,
           sizeof(result.channel[0].preview_q15));
    status = spectrum_health_check(&result, 9000u);
    assert((status & STATUS_SIGNAL_FROZEN) != 0u);
}

int main(void) {
    test_crc();
    test_exact_bin_spectrum();
    test_off_bin_interpolation();
    test_telemetry_round_trip();
    test_telemetry_magnitude_saturates();
    test_decoder_recovers_after_corruption();
    test_sequence_classification();
    test_acquisition_health();
    test_spectrum_health();
    test_signal_quality_flags();
    puts("core tests: PASS");
    return 0;
}

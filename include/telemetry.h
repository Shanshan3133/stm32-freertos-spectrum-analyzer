#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "analyzer_types.h"

#define TELEMETRY_PROTOCOL_VERSION       2u
#define TELEMETRY_TYPE_SPECTRUM          1u
#define TELEMETRY_SOF                  0x7Eu
#define TELEMETRY_ESC                  0x7Du
#define TELEMETRY_SPECTRUM_PAYLOAD_LEN 228u
#define TELEMETRY_RAW_SPECTRUM_LEN      236u
#define TELEMETRY_MAX_FRAME             474u

typedef struct {
    uint16_t sequence;
    uint32_t timestamp_us;
    uint32_t status;
    uint8_t channel;
    uint8_t chunk_index;
    uint8_t chunk_count;
    uint32_t sample_rate_hz;
    uint16_t bin_start;
    uint16_t bin_count;
    uint16_t rms_q15;
    uint16_t peak_q15;
    uint32_t dominant_millihz;
    uint32_t processing_us;
    uint32_t dropped_blocks;
    uint8_t magnitude_u8[ANALYZER_BINS_PER_CHUNK];
    int16_t preview_q15[ANALYZER_PREVIEW_PER_CHUNK];
} spectrum_chunk_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t format_errors;
    uint32_t overflow_errors;
    uint32_t escape_errors;
    uint32_t resync_events;
    uint32_t sequence_lost;
    uint32_t sequence_duplicates;
    uint32_t sequence_out_of_order;
} telemetry_decoder_stats_t;

typedef struct {
    uint8_t body[TELEMETRY_RAW_SPECTRUM_LEN];
    size_t length;
    uint16_t last_sequence;
    bool escaped;
    bool dropping;
    bool have_sequence;
    telemetry_decoder_stats_t stats;
} telemetry_decoder_t;

typedef enum {
    TELEMETRY_DECODE_NONE,
    TELEMETRY_DECODE_FRAME,
    TELEMETRY_DECODE_ERROR
} telemetry_decode_result_t;

uint16_t crc16_ccitt_false(const uint8_t *data, size_t length);
size_t telemetry_encode_spectrum_chunk(uint16_t sequence,
                                       const spectrum_result_t *result,
                                       uint8_t channel,
                                       uint8_t chunk_index,
                                       uint8_t *output,
                                       size_t output_capacity);
void telemetry_decoder_init(telemetry_decoder_t *decoder);
telemetry_decode_result_t telemetry_decoder_feed(
    telemetry_decoder_t *decoder, uint8_t byte, spectrum_chunk_t *chunk);

#endif

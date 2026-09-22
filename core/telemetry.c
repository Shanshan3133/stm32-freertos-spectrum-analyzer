#include "telemetry.h"

#include <string.h>

typedef enum { RAW_OK, RAW_BAD_CRC, RAW_BAD_FORMAT } raw_result_t;

static void put_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t crc16_ccitt_false(const uint8_t *data, size_t length) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) :
                                    (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool append_escaped(uint8_t byte, uint8_t *out, size_t capacity,
                           size_t *position) {
    if (byte == TELEMETRY_SOF || byte == TELEMETRY_ESC) {
        if (*position + 2u > capacity) return false;
        out[(*position)++] = TELEMETRY_ESC;
        out[(*position)++] = byte ^ 0x20u;
    } else {
        if (*position + 1u > capacity) return false;
        out[(*position)++] = byte;
    }
    return true;
}

size_t telemetry_encode_spectrum_chunk(uint16_t sequence,
                                       const spectrum_result_t *result,
                                       uint8_t channel,
                                       uint8_t chunk_index,
                                       uint8_t *output,
                                       size_t output_capacity) {
    if (result == NULL || output == NULL || channel >= ANALYZER_CHANNELS ||
        chunk_index >= ANALYZER_CHUNKS_PER_CHANNEL) return 0u;

    uint8_t raw[TELEMETRY_RAW_SPECTRUM_LEN];
    const channel_spectrum_t *source = &result->channel[channel];
    const uint16_t bin_start = (uint16_t)(chunk_index *
                                         ANALYZER_BINS_PER_CHUNK);
    raw[0] = TELEMETRY_PROTOCOL_VERSION;
    raw[1] = TELEMETRY_TYPE_SPECTRUM;
    put_u16(&raw[2], TELEMETRY_SPECTRUM_PAYLOAD_LEN);
    put_u16(&raw[4], sequence);
    put_u32(&raw[6], result->timestamp_us);
    put_u32(&raw[10], result->status);
    raw[14] = channel;
    raw[15] = chunk_index;
    raw[16] = ANALYZER_CHUNKS_PER_CHANNEL;
    raw[17] = 0u;
    put_u32(&raw[18], result->sample_rate_hz);
    put_u16(&raw[22], bin_start);
    put_u16(&raw[24], ANALYZER_BINS_PER_CHUNK);
    put_u16(&raw[26], source->rms_q15);
    put_u16(&raw[28], source->peak_q15);
    put_u32(&raw[30], source->dominant_millihz);
    put_u32(&raw[34], result->processing_us);
    put_u32(&raw[38], result->dropped_blocks);
    for (size_t i = 0u; i < ANALYZER_BINS_PER_CHUNK; ++i) {
        const uint16_t magnitude = source->magnitude_q15[bin_start + i];
        const uint16_t compressed = (uint16_t)((magnitude + 64u) >> 7);
        raw[42u + i] = (uint8_t)(compressed > 255u ? 255u : compressed);
    }
    const size_t preview_start = chunk_index * ANALYZER_PREVIEW_PER_CHUNK;
    for (size_t i = 0u; i < ANALYZER_PREVIEW_PER_CHUNK; ++i) {
        put_u16(&raw[170u + i * 2u],
                (uint16_t)source->preview_q15[preview_start + i]);
    }
    put_u16(&raw[234], crc16_ccitt_false(raw, 234u));

    if (output_capacity < 2u) return 0u;
    size_t position = 0u;
    output[position++] = TELEMETRY_SOF;
    for (size_t i = 0u; i < sizeof(raw); ++i) {
        if (!append_escaped(raw[i], output, output_capacity - 1u, &position)) {
            return 0u;
        }
    }
    if (position >= output_capacity) return 0u;
    output[position++] = TELEMETRY_SOF;
    return position;
}

static raw_result_t decode_body(const uint8_t *body, size_t length,
                                spectrum_chunk_t *chunk) {
    if (length != TELEMETRY_RAW_SPECTRUM_LEN) return RAW_BAD_FORMAT;
    if (crc16_ccitt_false(body, 234u) != get_u16(&body[234])) {
        return RAW_BAD_CRC;
    }
    if (body[0] != TELEMETRY_PROTOCOL_VERSION ||
        body[1] != TELEMETRY_TYPE_SPECTRUM ||
        get_u16(&body[2]) != TELEMETRY_SPECTRUM_PAYLOAD_LEN ||
        body[14] >= ANALYZER_CHANNELS ||
        body[15] >= ANALYZER_CHUNKS_PER_CHANNEL ||
        body[16] != ANALYZER_CHUNKS_PER_CHANNEL || body[17] != 0u ||
        get_u16(&body[24]) != ANALYZER_BINS_PER_CHUNK ||
        get_u16(&body[22]) !=
            (uint16_t)(body[15] * ANALYZER_BINS_PER_CHUNK)) {
        return RAW_BAD_FORMAT;
    }
    chunk->sequence = get_u16(&body[4]);
    chunk->timestamp_us = get_u32(&body[6]);
    chunk->status = get_u32(&body[10]);
    chunk->channel = body[14];
    chunk->chunk_index = body[15];
    chunk->chunk_count = body[16];
    chunk->sample_rate_hz = get_u32(&body[18]);
    chunk->bin_start = get_u16(&body[22]);
    chunk->bin_count = get_u16(&body[24]);
    chunk->rms_q15 = get_u16(&body[26]);
    chunk->peak_q15 = get_u16(&body[28]);
    chunk->dominant_millihz = get_u32(&body[30]);
    chunk->processing_us = get_u32(&body[34]);
    chunk->dropped_blocks = get_u32(&body[38]);
    for (size_t i = 0u; i < ANALYZER_BINS_PER_CHUNK; ++i) {
        chunk->magnitude_u8[i] = body[42u + i];
    }
    for (size_t i = 0u; i < ANALYZER_PREVIEW_PER_CHUNK; ++i) {
        chunk->preview_q15[i] =
            (int16_t)get_u16(&body[170u + i * 2u]);
    }
    return RAW_OK;
}

void telemetry_decoder_init(telemetry_decoder_t *decoder) {
    if (decoder != NULL) memset(decoder, 0, sizeof(*decoder));
}

telemetry_decode_result_t telemetry_decoder_feed(
    telemetry_decoder_t *decoder, uint8_t byte, spectrum_chunk_t *chunk) {
    if (decoder == NULL || chunk == NULL) return TELEMETRY_DECODE_ERROR;
    if (byte == TELEMETRY_SOF) {
        if (decoder->escaped) {
            ++decoder->stats.escape_errors;
            ++decoder->stats.resync_events;
            decoder->escaped = false;
            decoder->length = 0u;
            return TELEMETRY_DECODE_ERROR;
        }
        if (decoder->dropping) {
            ++decoder->stats.resync_events;
            decoder->dropping = false;
            decoder->length = 0u;
            return TELEMETRY_DECODE_ERROR;
        }
        if (decoder->length == 0u) return TELEMETRY_DECODE_NONE;
        const raw_result_t result = decode_body(decoder->body,
                                                decoder->length, chunk);
        decoder->length = 0u;
        if (result != RAW_OK) {
            if (result == RAW_BAD_CRC) ++decoder->stats.crc_errors;
            else ++decoder->stats.format_errors;
            ++decoder->stats.resync_events;
            return TELEMETRY_DECODE_ERROR;
        }
        if (decoder->have_sequence) {
            const uint16_t delta = (uint16_t)(chunk->sequence -
                                               decoder->last_sequence);
            if (delta == 0u) {
                ++decoder->stats.sequence_duplicates;
            } else if (delta < 0x8000u) {
                decoder->stats.sequence_lost += (uint16_t)(delta - 1u);
                decoder->last_sequence = chunk->sequence;
            } else {
                ++decoder->stats.sequence_out_of_order;
            }
        } else {
            decoder->last_sequence = chunk->sequence;
            decoder->have_sequence = true;
        }
        ++decoder->stats.frames_ok;
        return TELEMETRY_DECODE_FRAME;
    }
    if (decoder->dropping) return TELEMETRY_DECODE_NONE;
    if (decoder->escaped) {
        if (byte != (TELEMETRY_SOF ^ 0x20u) &&
            byte != (TELEMETRY_ESC ^ 0x20u)) {
            ++decoder->stats.escape_errors;
            decoder->dropping = true;
            decoder->escaped = false;
            decoder->length = 0u;
            return TELEMETRY_DECODE_ERROR;
        }
        byte ^= 0x20u;
        decoder->escaped = false;
    } else if (byte == TELEMETRY_ESC) {
        decoder->escaped = true;
        return TELEMETRY_DECODE_NONE;
    }
    if (decoder->length >= sizeof(decoder->body)) {
        ++decoder->stats.overflow_errors;
        decoder->dropping = true;
        decoder->length = 0u;
        return TELEMETRY_DECODE_ERROR;
    }
    decoder->body[decoder->length++] = byte;
    return TELEMETRY_DECODE_NONE;
}

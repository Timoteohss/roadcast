#include "roadcast_protocol.h"

#include <string.h>

static void put_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static void put_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static void put_u64(uint8_t *out, uint64_t value) {
    put_u32(out, (uint32_t)(value >> 32));
    put_u32(out + 4, (uint32_t)value);
}

static uint16_t get_u16(const uint8_t *in) {
    return (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
}

static uint32_t get_u32(const uint8_t *in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) | in[3];
}

static uint64_t get_u64(const uint8_t *in) {
    return ((uint64_t)get_u32(in) << 32) | get_u32(in + 4);
}

static void put_double(uint8_t *out, double value) {
    uint64_t bits;
    _Static_assert(sizeof(bits) == sizeof(value),
                   "Roadcast requires IEEE-754 binary64 doubles");
    memcpy(&bits, &value, sizeof(bits));
    put_u64(out, bits);
}

static double get_double(const uint8_t *in) {
    uint64_t bits = get_u64(in);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void roadcast_encode_header(uint8_t out[ROADCAST_HEADER_SIZE],
                            const roadcast_header_t *header) {
    put_u32(out, ROADCAST_MAGIC);
    put_u16(out + 4, header->version);
    put_u16(out + 6, header->type);
    put_u32(out + 8, header->flags);
    put_u32(out + 12, header->payload_bytes);
    put_u64(out + 16, header->sequence);
    put_u64(out + 24, header->timestamp_ns);
}

int roadcast_decode_header(const uint8_t in[ROADCAST_HEADER_SIZE],
                           roadcast_header_t *header) {
    if (get_u32(in) != ROADCAST_MAGIC)
        return -1;
    header->version = get_u16(in + 4);
    header->type = get_u16(in + 6);
    header->flags = get_u32(in + 8);
    header->payload_bytes = get_u32(in + 12);
    header->sequence = get_u64(in + 16);
    header->timestamp_ns = get_u64(in + 24);
    if (header->payload_bytes > ROADCAST_MAX_PAYLOAD)
        return -1;
    return 0;
}

size_t roadcast_encode_hello(uint8_t out[8]) {
    put_u16(out, ROADCAST_PROTOCOL_VERSION);
    put_u16(out + 2, ROADCAST_PROTOCOL_VERSION);
    put_u32(out + 4, ROADCAST_CAP_RAW_CAN | ROADCAST_CAP_DECODED_CAN);
    return 8;
}

int roadcast_decode_hello(const uint8_t *payload, size_t len,
                          uint16_t *min_version, uint16_t *max_version,
                          uint32_t *capabilities) {
    if (len != 8)
        return -1;
    *min_version = get_u16(payload);
    *max_version = get_u16(payload + 2);
    *capabilities = get_u32(payload + 4);
    return 0;
}

size_t roadcast_encode_welcome(uint8_t out[ROADCAST_WELCOME_SIZE], uint32_t hz,
                               uint32_t frame_count, uint32_t signal_count,
                               uint32_t max_payload, uint32_t capabilities,
                               uint32_t schema_version, uint64_t schema_hash) {
    put_u32(out, hz);
    put_u32(out + 4, frame_count);
    put_u32(out + 8, signal_count);
    put_u32(out + 12, max_payload);
    put_u32(out + 16, capabilities);
    put_u32(out + 20, schema_version);
    put_u64(out + 24, schema_hash);
    return ROADCAST_WELCOME_SIZE;
}

int roadcast_decode_welcome(const uint8_t *payload, size_t len, uint32_t *hz,
                            uint32_t *frame_count, uint32_t *signal_count,
                            uint32_t *max_payload, uint32_t *capabilities,
                            uint32_t *schema_version, uint64_t *schema_hash) {
    if (len != ROADCAST_WELCOME_SIZE)
        return -1;
    *hz = get_u32(payload);
    *frame_count = get_u32(payload + 4);
    *signal_count = get_u32(payload + 8);
    *max_payload = get_u32(payload + 12);
    *capabilities = get_u32(payload + 16);
    *schema_version = get_u32(payload + 20);
    *schema_hash = get_u64(payload + 24);
    return 0;
}

size_t roadcast_encode_index_request(uint8_t out[ROADCAST_SCHEMA_REQUEST_SIZE],
                                     uint32_t start_index) {
    put_u32(out, start_index);
    return ROADCAST_SCHEMA_REQUEST_SIZE;
}

int roadcast_decode_index_request(const uint8_t *payload, size_t len,
                                  uint32_t *start_index) {
    if (len != ROADCAST_SCHEMA_REQUEST_SIZE)
        return -1;
    *start_index = get_u32(payload);
    return 0;
}

int roadcast_decode_schema_chunk(const uint8_t *payload, size_t len,
                                 uint32_t *schema_version,
                                 uint32_t *total_count, uint32_t *start_index,
                                 uint64_t *schema_hash,
                                 roadcast_schema_entry_t *entries,
                                 size_t entry_capacity, size_t *entry_count) {
    if (len < ROADCAST_SCHEMA_CHUNK_PREFIX_SIZE)
        return -1;

    *schema_version = get_u32(payload);
    *total_count = get_u32(payload + 4);
    *start_index = get_u32(payload + 8);
    uint32_t count = get_u32(payload + 12);
    *schema_hash = get_u64(payload + 16);
    if (count > entry_capacity || *start_index > *total_count ||
        count > *total_count - *start_index)
        return -1;

    size_t offset = ROADCAST_SCHEMA_CHUNK_PREFIX_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        if (len - offset < ROADCAST_SCHEMA_ENTRY_FIXED_SIZE)
            return -1;
        const uint8_t *encoded = payload + offset;
        size_t name_length = get_u16(encoded + 22);
        size_t unit_length = get_u16(encoded + 24);
        if (get_u16(encoded + 26) != 0 ||
            name_length > ROADCAST_SCHEMA_NAME_MAX ||
            unit_length > ROADCAST_SCHEMA_UNIT_MAX ||
            name_length + unit_length >
                len - offset - ROADCAST_SCHEMA_ENTRY_FIXED_SIZE)
            return -1;

        roadcast_schema_entry_t *entry = &entries[i];
        entry->stable_id = get_u64(encoded);
        entry->index = get_u32(encoded + 8);
        entry->invalid_signal_index = get_u32(encoded + 12);
        entry->can_id = get_u16(encoded + 16);
        entry->kind = encoded[18];
        entry->source = encoded[19];
        entry->width = encoded[20];
        entry->flags = encoded[21];
        entry->scale = get_double(encoded + 28);
        entry->offset = get_double(encoded + 36);
        memcpy(entry->name, encoded + ROADCAST_SCHEMA_ENTRY_FIXED_SIZE,
               name_length);
        entry->name[name_length] = '\0';
        memcpy(entry->unit,
               encoded + ROADCAST_SCHEMA_ENTRY_FIXED_SIZE + name_length,
               unit_length);
        entry->unit[unit_length] = '\0';
        if (entry->index != *start_index + i ||
            entry->kind != ROADCAST_ITEM_CAN_SIGNAL ||
            entry->source != ROADCAST_SOURCE_RAW_CAN || entry->width == 0 ||
            entry->width > 64 ||
            (entry->invalid_signal_index >= *total_count &&
             entry->invalid_signal_index != UINT32_MAX))
            return -1;
        offset += ROADCAST_SCHEMA_ENTRY_FIXED_SIZE + name_length + unit_length;
    }
    if (offset != len)
        return -1;
    *entry_count = count;
    return 0;
}

size_t roadcast_encode_frame_batch(uint8_t *out, size_t cap,
                                   uint64_t sample_sequence,
                                   const roadcast_frame_t *frames,
                                   const uint16_t *indices, size_t count) {
    if (count > UINT16_MAX)
        return 0;
    if (count >
        (SIZE_MAX - ROADCAST_BATCH_PREFIX_SIZE) / ROADCAST_FRAME_WIRE_SIZE)
        return 0;
    size_t needed =
        ROADCAST_BATCH_PREFIX_SIZE + count * ROADCAST_FRAME_WIRE_SIZE;
    if (needed > cap)
        return 0;

    put_u64(out, sample_sequence);
    put_u16(out + 8, (uint16_t)count);
    put_u16(out + 10, 0);
    for (size_t i = 0; i < count; i++) {
        const roadcast_frame_t *frame = &frames[indices ? indices[i] : i];
        uint8_t *record =
            out + ROADCAST_BATCH_PREFIX_SIZE + i * ROADCAST_FRAME_WIRE_SIZE;
        put_u16(record, frame->can_id);
        record[2] = frame->present ? 1u : 0u;
        record[3] = 0;
        memcpy(record + 4, frame->data, sizeof(frame->data));
    }
    return needed;
}

int roadcast_decode_frame_batch(const uint8_t *payload, size_t len,
                                uint64_t *sample_sequence,
                                roadcast_frame_t *frames, size_t frame_cap,
                                size_t *frame_count) {
    if (len < ROADCAST_BATCH_PREFIX_SIZE)
        return -1;
    if (payload[10] != 0 || payload[11] != 0)
        return -1;
    size_t count = get_u16(payload + 8);
    if (count >
        (SIZE_MAX - ROADCAST_BATCH_PREFIX_SIZE) / ROADCAST_FRAME_WIRE_SIZE)
        return -1;
    size_t expected =
        ROADCAST_BATCH_PREFIX_SIZE + count * ROADCAST_FRAME_WIRE_SIZE;
    if (expected != len || count > frame_cap)
        return -1;

    *sample_sequence = get_u64(payload);
    *frame_count = count;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *record =
            payload + ROADCAST_BATCH_PREFIX_SIZE + i * ROADCAST_FRAME_WIRE_SIZE;
        if (record[2] > 1 || record[3] != 0)
            return -1;
        frames[i].can_id = get_u16(record);
        frames[i].present = record[2];
        memcpy(frames[i].data, record + 4, sizeof(frames[i].data));
    }
    return 0;
}

size_t roadcast_encode_signal_batch(uint8_t *out, size_t capacity,
                                    uint64_t sample_sequence,
                                    const roadcast_signal_value_t *signals,
                                    const uint32_t *indices, size_t count) {
    if (count > UINT16_MAX || count > (SIZE_MAX - ROADCAST_BATCH_PREFIX_SIZE) /
                                          ROADCAST_SIGNAL_WIRE_SIZE)
        return 0;
    size_t needed =
        ROADCAST_BATCH_PREFIX_SIZE + count * ROADCAST_SIGNAL_WIRE_SIZE;
    if (needed > capacity)
        return 0;

    put_u64(out, sample_sequence);
    put_u16(out + 8, (uint16_t)count);
    put_u16(out + 10, 0);
    for (size_t i = 0; i < count; i++) {
        uint32_t index = indices ? indices[i] : (uint32_t)i;
        const roadcast_signal_value_t *value = &signals[index];
        uint8_t *record =
            out + ROADCAST_BATCH_PREFIX_SIZE + i * ROADCAST_SIGNAL_WIRE_SIZE;
        put_u32(record, index);
        record[4] = value->valid ? 1 : 0;
        record[5] = value->calibrated ? 1 : 0;
        put_u16(record + 6, 0);
        put_u64(record + 8, value->raw);
        put_double(record + 16, value->physical);
    }
    return needed;
}

int roadcast_decode_signal_batch(const uint8_t *payload, size_t len,
                                 uint64_t *sample_sequence,
                                 roadcast_signal_update_t *updates,
                                 size_t update_capacity, size_t *update_count) {
    if (len < ROADCAST_BATCH_PREFIX_SIZE || payload[10] != 0 ||
        payload[11] != 0)
        return -1;
    size_t count = get_u16(payload + 8);
    if (count >
        (SIZE_MAX - ROADCAST_BATCH_PREFIX_SIZE) / ROADCAST_SIGNAL_WIRE_SIZE)
        return -1;
    size_t expected =
        ROADCAST_BATCH_PREFIX_SIZE + count * ROADCAST_SIGNAL_WIRE_SIZE;
    if (expected != len || count > update_capacity)
        return -1;

    *sample_sequence = get_u64(payload);
    *update_count = count;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *record = payload + ROADCAST_BATCH_PREFIX_SIZE +
                                i * ROADCAST_SIGNAL_WIRE_SIZE;
        if (record[4] > 1 || record[5] > 1 || get_u16(record + 6) != 0)
            return -1;
        updates[i].index = get_u32(record);
        updates[i].value.valid = record[4];
        updates[i].value.calibrated = record[5];
        updates[i].value.raw = get_u64(record + 8);
        updates[i].value.physical = get_double(record + 16);
    }
    return 0;
}

size_t roadcast_encode_heartbeat(uint8_t out[16], uint64_t sample_sequence,
                                 uint64_t dropped_batches) {
    put_u64(out, sample_sequence);
    put_u64(out + 8, dropped_batches);
    return 16;
}

int roadcast_decode_heartbeat(const uint8_t *payload, size_t len,
                              uint64_t *sample_sequence,
                              uint64_t *dropped_batches) {
    if (len != 16)
        return -1;
    *sample_sequence = get_u64(payload);
    *dropped_batches = get_u64(payload + 8);
    return 0;
}

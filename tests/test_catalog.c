#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "roadcast_catalog.h"

static uint32_t find_signal(uint16_t can_id, const char *name) {
    for (uint32_t i = 0; i < ROADCAST_SIGNAL_COUNT; i++) {
        if (ROADCAST_SIGNALS[i].can_id == can_id &&
            strcmp(ROADCAST_SIGNALS[i].name, name) == 0)
            return i;
    }
    return UINT32_MAX;
}

static void initialize_frames(roadcast_frame_t frames[ROADCAST_FRAME_COUNT]) {
    memset(frames, 0, sizeof(roadcast_frame_t) * ROADCAST_FRAME_COUNT);
    for (size_t i = 0; i < ROADCAST_FRAME_COUNT; i++) {
        frames[i].can_id = ROADCAST_CAN_IDS[i];
        frames[i].present = 1;
    }
}

static void test_catalog_identity(void) {
    assert(ROADCAST_SIGNAL_COUNT == 815);
    assert(ROADCAST_CAN_SCHEMA_HASH != 0);
    assert(ROADCAST_SIGNALS[0].stable_id == UINT64_C(0x35bcb28678e8e5b8));
    for (uint32_t i = 0; i < ROADCAST_SIGNAL_COUNT; i++) {
        assert(ROADCAST_SIGNALS[i].stable_id != 0);
        assert(ROADCAST_SIGNALS[i].frame_index < ROADCAST_FRAME_COUNT);
        assert(ROADCAST_CAN_IDS[ROADCAST_SIGNALS[i].frame_index] ==
               ROADCAST_SIGNALS[i].can_id);
        assert(ROADCAST_SIGNALS[i].width > 0);
        assert(ROADCAST_SIGNALS[i].width <= 64);
        for (uint32_t j = i + 1; j < ROADCAST_SIGNAL_COUNT; j++) {
            assert(ROADCAST_SIGNALS[i].stable_id !=
                   ROADCAST_SIGNALS[j].stable_id);
        }
    }

    uint32_t diagnostic_broadcast = find_signal(0x7df, "Diag_req");
    uint32_t diagnostic_module = find_signal(0x7c1, "Diag_req");
    assert(diagnostic_broadcast != UINT32_MAX);
    assert(diagnostic_module != UINT32_MAX);
    assert(ROADCAST_SIGNALS[diagnostic_broadcast].stable_id !=
           ROADCAST_SIGNALS[diagnostic_module].stable_id);
}

static void test_decoding_and_invalid_flag(void) {
    roadcast_frame_t frames[ROADCAST_FRAME_COUNT];
    roadcast_signal_value_t values[ROADCAST_SIGNAL_COUNT];
    initialize_frames(frames);

    uint32_t ambient = find_signal(0x2f1, "AC_AmbientTemperature");
    uint32_t ambient_invalid =
        find_signal(0x2f1, "AC_AmbientTemperatureInvalid");
    assert(ambient != UINT32_MAX);
    assert(ambient_invalid != UINT32_MAX);
    uint16_t frame_index = ROADCAST_SIGNALS[ambient].frame_index;
    frames[frame_index].data[1] = 42;
    roadcast_decode_signals(frames, values);
    assert(values[ambient].raw == 42);
    assert(values[ambient].physical == 42.0);
    assert(values[ambient].valid == 1);

    frames[frame_index].data[0] |= 2;
    roadcast_decode_signals(frames, values);
    assert(values[ambient_invalid].raw == 1);
    assert(values[ambient].valid == 0);
}

static void test_64_bit_signal(void) {
    roadcast_frame_t frames[ROADCAST_FRAME_COUNT];
    roadcast_signal_value_t values[ROADCAST_SIGNAL_COUNT];
    initialize_frames(frames);

    uint32_t diagnostic = find_signal(0x7df, "Diag_req");
    assert(diagnostic != UINT32_MAX);
    uint16_t frame_index = ROADCAST_SIGNALS[diagnostic].frame_index;
    for (uint8_t i = 0; i < 8; i++)
        frames[frame_index].data[i] = (uint8_t)(i + 1);
    roadcast_decode_signals(frames, values);
    assert(values[diagnostic].raw == UINT64_C(0x0102030405060708));
}

static void test_signed_63_bit_conversion(void) {
    roadcast_signal_definition_t definition = {
        .width = 63,
        .is_signed = 1,
        .scale = 1.0,
        .offset = 0.0,
    };
    assert(roadcast_decode_signal_physical(&definition,
                                           UINT64_C(0x4000000000000000)) ==
           -4611686018427387904.0);
    assert(roadcast_decode_signal_physical(
               &definition, UINT64_C(0x7fffffffffffffff)) == -1.0);
}

static void test_schema_round_trip(void) {
    uint8_t payload[2048];
    uint32_t encoded_count;
    size_t length = roadcast_encode_schema_chunk(payload, sizeof(payload), 0,
                                                 &encoded_count);
    assert(length > ROADCAST_SCHEMA_CHUNK_PREFIX_SIZE);
    assert(encoded_count > 0);

    roadcast_schema_entry_t decoded[64];
    uint32_t version;
    uint32_t total_count;
    uint32_t start_index;
    uint64_t hash;
    size_t decoded_count;
    assert(roadcast_decode_schema_chunk(payload, length, &version, &total_count,
                                        &start_index, &hash, decoded, 64,
                                        &decoded_count) == 0);
    assert(version == ROADCAST_SCHEMA_VERSION);
    assert(total_count == ROADCAST_SIGNAL_COUNT);
    assert(start_index == 0);
    assert(hash == ROADCAST_CAN_SCHEMA_HASH);
    assert(decoded_count == encoded_count);
    assert(decoded[0].stable_id == ROADCAST_SIGNALS[0].stable_id);
    assert(strcmp(decoded[0].name, ROADCAST_SIGNALS[0].name) == 0);
}

int main(void) {
    test_catalog_identity();
    test_decoding_and_invalid_flag();
    test_64_bit_signal();
    test_signed_63_bit_conversion();
    test_schema_round_trip();
    puts("catalog tests passed");
    return 0;
}

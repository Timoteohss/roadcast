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
        frames[i].state = ROADCAST_OBSERVATION_VALID;
        frames[i].first_observed_ns = 10;
        frames[i].last_change_ns = 20;
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
    assert(values[ambient].state == ROADCAST_OBSERVATION_VALID);
    assert(values[ambient].first_observed_ns == 10);
    assert(values[ambient].last_change_ns == 20);

    frames[frame_index].data[0] |= 2;
    roadcast_decode_signals(frames, values);
    assert(values[ambient_invalid].raw == 1);
    assert(values[ambient].state == ROADCAST_OBSERVATION_INVALID);

    frames[frame_index].state = ROADCAST_OBSERVATION_NEVER_OBSERVED;
    roadcast_decode_signals(frames, values);
    assert(values[ambient].state == ROADCAST_OBSERVATION_NEVER_OBSERVED);

    frames[frame_index].state = ROADCAST_OBSERVATION_UNAVAILABLE;
    roadcast_decode_signals(frames, values);
    assert(values[ambient].state == ROADCAST_OBSERVATION_UNAVAILABLE);
}

static void test_calibrated_drive_power(void) {
    roadcast_frame_t frames[ROADCAST_FRAME_COUNT];
    roadcast_signal_value_t values[ROADCAST_SIGNAL_COUNT];
    initialize_frames(frames);

    uint32_t drive_power = find_signal(0x315, "VCU_DrvPwrAct");
    assert(drive_power != UINT32_MAX);
    assert(ROADCAST_SIGNALS[drive_power].calibrated == 1);
    assert(ROADCAST_SIGNALS[drive_power].scale == 0.1);
    assert(ROADCAST_SIGNALS[drive_power].offset == -204.8);

    roadcast_decode_signals(frames, values);
    assert(values[drive_power].calibrated == 1);
}

static void test_calibrated_obc_input_voltage(void) {
    roadcast_frame_t frames[ROADCAST_FRAME_COUNT];
    roadcast_signal_value_t values[ROADCAST_SIGNAL_COUNT];
    initialize_frames(frames);

    uint32_t input_voltage = find_signal(0x221, "OBC_uInAct");
    assert(input_voltage != UINT32_MAX);
    assert(ROADCAST_SIGNALS[input_voltage].calibrated == 1);
    assert(ROADCAST_SIGNALS[input_voltage].scale == 0.1);
    assert(ROADCAST_SIGNALS[input_voltage].offset == 0.0);
    assert(strcmp(ROADCAST_SIGNALS[input_voltage].unit, "V") == 0);

    uint16_t frame_index = ROADCAST_SIGNALS[input_voltage].frame_index;
    frames[frame_index].data[5] = 8;
    frames[frame_index].data[6] = 157;
    roadcast_decode_signals(frames, values);
    assert(values[input_voltage].raw == 2205);
    assert(values[input_voltage].physical == 220.5);
    assert(values[input_voltage].calibrated == 1);
}

static void test_calibrated_obc_input_current(void) {
    roadcast_frame_t frames[ROADCAST_FRAME_COUNT];
    roadcast_signal_value_t values[ROADCAST_SIGNAL_COUNT];
    initialize_frames(frames);

    uint32_t input_current = find_signal(0x221, "OBC_iInAct");
    assert(input_current != UINT32_MAX);
    assert(ROADCAST_SIGNALS[input_current].calibrated == 1);
    assert(ROADCAST_SIGNALS[input_current].scale == 0.1);
    assert(ROADCAST_SIGNALS[input_current].offset == 0.0);
    assert(strcmp(ROADCAST_SIGNALS[input_current].unit, "A") == 0);

    uint16_t frame_index = ROADCAST_SIGNALS[input_current].frame_index;
    frames[frame_index].data[4] = 255;
    frames[frame_index].data[5] = 192;
    roadcast_decode_signals(frames, values);
    assert(values[input_current].raw == 1023);
    assert(values[input_current].physical > 102.29);
    assert(values[input_current].physical < 102.31);
    assert(values[input_current].calibrated == 1);
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
    test_calibrated_drive_power();
    test_calibrated_obc_input_voltage();
    test_calibrated_obc_input_current();
    test_64_bit_signal();
    test_signed_63_bit_conversion();
    test_schema_round_trip();
    puts("catalog tests passed");
    return 0;
}

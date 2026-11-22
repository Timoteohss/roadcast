#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "roadcast_buckets.h"

static int near(double actual, double expected, double eps) {
    return fabs(actual - expected) <= eps;
}

static roadcast_signal_value_t valid_sig(double physical) {
    roadcast_signal_value_t s = {0};
    s.physical = physical;
    s.state = ROADCAST_OBSERVATION_VALID;
    s.calibrated = 1;
    return s;
}

static void test_constant_signal_and_minute_boundary_split(void) {
    uint32_t indices[] = {0};
    roadcast_bucket_store_t store;
    roadcast_bucket_store_init(&store, indices, 1);

    /* Start at 58s (inside bucket 0), step 1s at constant 10 kW across 60s into 62s (bucket 1). */
    roadcast_signal_value_t sigs[1] = {valid_sig(10.0)};
    for (uint64_t sec = 58; sec <= 62; sec++) {
        roadcast_bucket_store_ingest(&store, sigs, 1, sec * UINT64_C(1000000000));
    }

    assert(roadcast_bucket_store_count(&store, 0, UINT32_MAX) == 2);
    roadcast_signal_bucket_t out[4];
    size_t n = roadcast_bucket_store_query(&store, 0, UINT32_MAX, 0, out, 4);
    assert(n == 2);

    /* Bucket 0 covers [58s..60s] = 2s * 10 kW = 20 kW*s. */
    assert(out[0].bucket_id == 0);
    assert(out[0].start_monotonic_ns == 0);
    assert(out[0].covered_ms == 2000u);
    assert(near(out[0].pos_integral, 20.0, 1e-6));
    assert(near(out[0].neg_integral, 0.0, 1e-6));
    assert(near(out[0].first_val, 10.0, 1e-6));
    assert(near(out[0].last_val, 10.0, 1e-6));

    /* Bucket 1 covers [60s..62s] = 2s * 10 kW = 20 kW*s. */
    assert(out[1].bucket_id == 1);
    assert(out[1].start_monotonic_ns == ROADCAST_BUCKET_WIDTH_NS);
    assert(out[1].covered_ms == 2000u);
    assert(near(out[1].pos_integral, 20.0, 1e-6));
    assert(near(out[1].neg_integral, 0.0, 1e-6));
}

static void test_bipolar_zero_crossing_and_boundary_interpolation(void) {
    uint32_t indices[] = {2};
    roadcast_bucket_store_t store;
    roadcast_bucket_store_init(&store, indices, 1);

    roadcast_signal_value_t sigs[3] = {0};
    /* Linear ramp from +10 kW at t=10s to -10 kW at t=12s -> crosses 0 at t=11s. */
    sigs[2] = valid_sig(10.0);
    roadcast_bucket_store_ingest(&store, sigs, 3, UINT64_C(10000000000));
    sigs[2] = valid_sig(-10.0);
    roadcast_bucket_store_ingest(&store, sigs, 3, UINT64_C(12000000000));

    roadcast_signal_bucket_t out[2];
    size_t n = roadcast_bucket_store_query(&store, 0, 2, 0, out, 2);
    assert(n == 1);
    /* Positive triangle: 0.5 * 10 * 1s = 5.0; negative triangle: 0.5 * 10 * 1s = 5.0. */
    assert(near(out[0].pos_integral, 5.0, 1e-6));
    assert(near(out[0].neg_integral, 5.0, 1e-6));
    assert(near(out[0].min_val, -10.0, 1e-6));
    assert(near(out[0].max_val, 10.0, 1e-6));
    assert(near(out[0].first_val, 10.0, 1e-6));
    assert(near(out[0].last_val, -10.0, 1e-6));
    assert(out[0].covered_ms == 2000u);
}

static void test_gap_rejection_and_invalid_signal_ignored(void) {
    uint32_t indices[] = {0};
    roadcast_bucket_store_t store;
    roadcast_bucket_store_init(&store, indices, 1);

    roadcast_signal_value_t sigs[1] = {valid_sig(20.0)};
    roadcast_bucket_store_ingest(&store, sigs, 1, UINT64_C(1000000000));
    /* Gap of 10s (> 5s max gap): must NOT integrate 200 kW*s across missing data. */
    roadcast_bucket_store_ingest(&store, sigs, 1, UINT64_C(11000000000));
    /* 1s valid step after gap: 20 kW * 1s = 20 kW*s. */
    roadcast_bucket_store_ingest(&store, sigs, 1, UINT64_C(12000000000));
    /* Invalid signal resets continuity. */
    sigs[0].state = ROADCAST_OBSERVATION_INVALID;
    roadcast_bucket_store_ingest(&store, sigs, 1, UINT64_C(13000000000));

    roadcast_signal_bucket_t out[2];
    size_t n = roadcast_bucket_store_query(&store, 0, 0, 0, out, 2);
    assert(n == 1);
    assert(near(out[0].pos_integral, 20.0, 1e-6));
    assert(out[0].covered_ms == 1000u);
}

static void test_ring_buffer_wraparound_and_query_filtering(void) {
    uint32_t indices[] = {1, 4};
    roadcast_bucket_store_t store;
    roadcast_bucket_store_init(&store, indices, 2);

    roadcast_signal_value_t sigs[5] = {0};
    /* Feed 200 distinct minutes (exceeds 180 ring capacity). */
    for (uint64_t m = 1; m <= 200; m++) {
        sigs[1] = valid_sig((double)m);
        sigs[4] = valid_sig(-(double)m);
        uint64_t t = m * ROADCAST_BUCKET_WIDTH_NS + UINT64_C(1000000000);
        roadcast_bucket_store_ingest(&store, sigs, 5, t);
        roadcast_bucket_store_ingest(&store, sigs, 5, t + UINT64_C(1000000000));
    }

    /* Ring retains buckets [21..200] = 180 minutes * 2 channels = 360 buckets. */
    assert(roadcast_bucket_store_count(&store, 0, UINT32_MAX) == 360);
    assert(roadcast_bucket_store_count(&store, 198, 4) == 3);

    roadcast_signal_bucket_t out[4];
    size_t n = roadcast_bucket_store_query(&store, 198, 4, 1, out, 4);
    assert(n == 2);
    assert(out[0].bucket_id == 199);
    assert(out[0].signal_index == 4);
    assert(near(out[0].neg_integral, 199.0, 1e-6));
    assert(out[1].bucket_id == 200);
    assert(near(out[1].neg_integral, 200.0, 1e-6));
}

int main(void) {
    test_constant_signal_and_minute_boundary_split();
    test_bipolar_zero_crossing_and_boundary_interpolation();
    test_gap_rejection_and_invalid_signal_ignored();
    test_ring_buffer_wraparound_and_query_filtering();
    printf("bucket tests passed\n");
    return 0;
}

#ifndef ROADCAST_BUCKETS_H
#define ROADCAST_BUCKETS_H

#include <stddef.h>
#include <stdint.h>

#include "roadcast_protocol.h"

#define ROADCAST_BUCKET_WIDTH_NS UINT64_C(60000000000)
#define ROADCAST_BUCKET_MAX_GAP_NS UINT64_C(5000000000)
#define ROADCAST_BUCKET_RING_SLOTS 180u
#define ROADCAST_BUCKET_MAX_SIGNALS 16u

typedef struct {
    uint32_t signal_index;
    int has_previous;
    uint64_t previous_ns;
    double previous_val;
    roadcast_signal_bucket_t slots[ROADCAST_BUCKET_RING_SLOTS];
    uint8_t slot_used[ROADCAST_BUCKET_RING_SLOTS];
    uint64_t latest_bucket_id;
    int has_bucket;
} roadcast_signal_bucket_channel_t;

typedef struct {
    size_t channel_count;
    roadcast_signal_bucket_channel_t channels[ROADCAST_BUCKET_MAX_SIGNALS];
    uint64_t latest_bucket_id;
    int has_latest_bucket;
} roadcast_bucket_store_t;

void roadcast_bucket_store_init(roadcast_bucket_store_t *store,
                                const uint32_t *signal_indices,
                                size_t signal_count);

/*
 * Integrates one canonical signal snapshot at monotonic timestamp_ns.
 * Must be called on every acquisition cycle (including cycles with no raw
 * frame delta) so steady-state power/speed signals integrate continuously.
 */
void roadcast_bucket_store_ingest(roadcast_bucket_store_t *store,
                                  const roadcast_signal_value_t *signals,
                                  size_t signal_count, uint64_t timestamp_ns);

/*
 * Counts buckets matching (bucket_id >= after_bucket_id) and signal_index
 * (or all channels when signal_index == UINT32_MAX), ordered by
 * (bucket_id ASC, signal_index ASC).
 */
size_t roadcast_bucket_store_count(const roadcast_bucket_store_t *store,
                                   uint64_t after_bucket_id,
                                   uint32_t signal_index);

/*
 * Copies a slice starting at start_offset of the matching buckets into out,
 * returning the number of entries written.
 */
size_t roadcast_bucket_store_query(const roadcast_bucket_store_t *store,
                                   uint64_t after_bucket_id,
                                   uint32_t signal_index, size_t start_offset,
                                   roadcast_signal_bucket_t *out,
                                   size_t out_capacity);

#endif

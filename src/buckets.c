#include "roadcast_buckets.h"

#include <math.h>
#include <string.h>

static roadcast_signal_bucket_t *
ensure_bucket(roadcast_signal_bucket_channel_t *ch, uint64_t bucket_id,
              double sample_val) {
    size_t slot = (size_t)(bucket_id % ROADCAST_BUCKET_RING_SLOTS);
    roadcast_signal_bucket_t *b = &ch->slots[slot];
    if (!ch->slot_used[slot] || b->bucket_id != bucket_id) {
        memset(b, 0, sizeof(*b));
        b->bucket_id = bucket_id;
        b->start_monotonic_ns = bucket_id * ROADCAST_BUCKET_WIDTH_NS;
        b->signal_index = ch->signal_index;
        b->min_val = sample_val;
        b->max_val = sample_val;
        b->first_val = sample_val;
        b->last_val = sample_val;
        ch->slot_used[slot] = 1;
    } else {
        if (sample_val < b->min_val)
            b->min_val = sample_val;
        if (sample_val > b->max_val)
            b->max_val = sample_val;
        b->last_val = sample_val;
    }
    if (!ch->has_bucket || bucket_id > ch->latest_bucket_id) {
        ch->latest_bucket_id = bucket_id;
        ch->has_bucket = 1;
    }
    return b;
}

static void add_segment_in_bucket(roadcast_signal_bucket_t *b, double v0,
                                  double v1, uint64_t dt_ns) {
    if (dt_ns == 0)
        return;
    double dt_s = (double)dt_ns / 1e9;
    if (v0 >= 0.0 && v1 >= 0.0) {
        b->pos_integral += 0.5 * (v0 + v1) * dt_s;
    } else if (v0 <= 0.0 && v1 <= 0.0) {
        b->neg_integral += 0.5 * (-v0 - v1) * dt_s;
    } else if (v0 > 0.0 && v1 < 0.0) {
        /* Exact zero crossing: split positive and negative triangles. */
        double frac_pos = v0 / (v0 - v1);
        double dt_pos = dt_s * frac_pos;
        double dt_neg = dt_s - dt_pos;
        b->pos_integral += 0.5 * v0 * dt_pos;
        b->neg_integral += 0.5 * (-v1) * dt_neg;
    } else {
        /* v0 < 0.0 && v1 > 0.0 */
        double frac_neg = (-v0) / (v1 - v0);
        double dt_neg = dt_s * frac_neg;
        double dt_pos = dt_s - dt_neg;
        b->neg_integral += 0.5 * (-v0) * dt_neg;
        b->pos_integral += 0.5 * v1 * dt_pos;
    }

    uint64_t add_ms = (dt_ns + UINT64_C(500000)) / UINT64_C(1000000);
    uint64_t next_ms = (uint64_t)b->covered_ms + add_ms;
    b->covered_ms = next_ms > 60000u ? 60000u : (uint32_t)next_ms;
}

static void integrate_interval(roadcast_signal_bucket_channel_t *ch,
                               uint64_t t0, double v0, uint64_t t1, double v1) {
    uint64_t total_dt = t1 - t0;
    if (total_dt == 0)
        return;

    uint64_t cur_t = t0;
    double cur_v = v0;
    while (cur_t < t1) {
        uint64_t bucket_id = cur_t / ROADCAST_BUCKET_WIDTH_NS;
        uint64_t bucket_end = (bucket_id + 1u) * ROADCAST_BUCKET_WIDTH_NS;
        uint64_t seg_end = t1 < bucket_end ? t1 : bucket_end;
        double seg_end_v;
        if (seg_end == t1) {
            seg_end_v = v1;
        } else {
            double frac = (double)(seg_end - t0) / (double)total_dt;
            seg_end_v = v0 + (v1 - v0) * frac;
        }
        roadcast_signal_bucket_t *b = ensure_bucket(ch, bucket_id, cur_v);
        if (seg_end_v < b->min_val)
            b->min_val = seg_end_v;
        if (seg_end_v > b->max_val)
            b->max_val = seg_end_v;
        b->last_val = seg_end_v;
        add_segment_in_bucket(b, cur_v, seg_end_v, seg_end - cur_t);

        cur_t = seg_end;
        cur_v = seg_end_v;
    }
}

void roadcast_bucket_store_init(roadcast_bucket_store_t *store,
                                const uint32_t *signal_indices,
                                size_t signal_count) {
    if (!store)
        return;
    memset(store, 0, sizeof(*store));
    if (!signal_indices)
        return;
    size_t limit = signal_count < ROADCAST_BUCKET_MAX_SIGNALS
                       ? signal_count
                       : ROADCAST_BUCKET_MAX_SIGNALS;
    for (size_t i = 0; i < limit; i++) {
        store->channels[i].signal_index = signal_indices[i];
    }
    store->channel_count = limit;
}

void roadcast_bucket_store_ingest(roadcast_bucket_store_t *store,
                                  const roadcast_signal_value_t *signals,
                                  size_t signal_count, uint64_t timestamp_ns) {
    if (!store || !signals || timestamp_ns == 0)
        return;

    uint64_t current_bucket_id = timestamp_ns / ROADCAST_BUCKET_WIDTH_NS;
    if (!store->has_latest_bucket ||
        current_bucket_id > store->latest_bucket_id) {
        store->latest_bucket_id = current_bucket_id;
        store->has_latest_bucket = 1;
    }

    for (size_t i = 0; i < store->channel_count; i++) {
        roadcast_signal_bucket_channel_t *ch = &store->channels[i];
        if (ch->signal_index >= signal_count)
            continue;
        const roadcast_signal_value_t *sig = &signals[ch->signal_index];
        if (sig->state != ROADCAST_OBSERVATION_VALID || !sig->calibrated ||
            !isfinite(sig->physical)) {
            ch->has_previous = 0;
            continue;
        }
        double val = sig->physical;
        if (ch->has_previous && timestamp_ns > ch->previous_ns) {
            uint64_t dt = timestamp_ns - ch->previous_ns;
            if (dt <= ROADCAST_BUCKET_MAX_GAP_NS) {
                integrate_interval(ch, ch->previous_ns, ch->previous_val,
                                   timestamp_ns, val);
            } else {
                ensure_bucket(ch, current_bucket_id, val);
            }
        } else {
            ensure_bucket(ch, current_bucket_id, val);
        }
        ch->has_previous = 1;
        ch->previous_ns = timestamp_ns;
        ch->previous_val = val;
    }
}

static size_t collect_matching_sorted(const roadcast_bucket_store_t *store,
                                      uint64_t after_bucket_id,
                                      uint32_t signal_index,
                                      size_t start_offset,
                                      roadcast_signal_bucket_t *out,
                                      size_t out_capacity) {
    if (!store || !store->has_latest_bucket)
        return 0;

    uint64_t newest = store->latest_bucket_id;
    uint64_t oldest = newest + 1u > ROADCAST_BUCKET_RING_SLOTS
                          ? newest + 1u - ROADCAST_BUCKET_RING_SLOTS
                          : 0u;
    uint64_t first = after_bucket_id > oldest ? after_bucket_id : oldest;
    if (first > newest)
        return 0;

    size_t matched = 0;
    size_t written = 0;
    for (uint64_t bid = first; bid <= newest; bid++) {
        size_t slot = (size_t)(bid % ROADCAST_BUCKET_RING_SLOTS);
        for (size_t i = 0; i < store->channel_count; i++) {
            const roadcast_signal_bucket_channel_t *ch = &store->channels[i];
            if (signal_index != UINT32_MAX && ch->signal_index != signal_index)
                continue;
            if (!ch->slot_used[slot] || ch->slots[slot].bucket_id != bid)
                continue;
            if (out) {
                if (matched >= start_offset && written < out_capacity) {
                    out[written++] = ch->slots[slot];
                }
            }
            matched++;
        }
    }
    return out ? written : matched;
}

size_t roadcast_bucket_store_count(const roadcast_bucket_store_t *store,
                                   uint64_t after_bucket_id,
                                   uint32_t signal_index) {
    return collect_matching_sorted(store, after_bucket_id, signal_index, 0,
                                   NULL, 0);
}

size_t roadcast_bucket_store_query(const roadcast_bucket_store_t *store,
                                   uint64_t after_bucket_id,
                                   uint32_t signal_index, size_t start_offset,
                                   roadcast_signal_bucket_t *out,
                                   size_t out_capacity) {
    if (!out || !out_capacity)
        return 0;
    return collect_matching_sorted(store, after_bucket_id, signal_index,
                                   start_offset, out, out_capacity);
}

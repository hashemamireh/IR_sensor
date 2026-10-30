#pragma once

// Live view (spec §3.6): the ADC runs at full rate, but instead of storing
// samples the device reduces each 20 ms slice to its min, max and mean
// ("bucket"), so short pulses still show as spikes on a slow display. Buckets
// go into a ring with sequence numbers; the app fetches everything since the
// last one it saw, and the OLED draws the newest ones.

#include <cstddef>
#include <cstdint>

#include "json_writer.h"

namespace live {

constexpr uint32_t SAMPLE_RATE_HZ     = 10000;
constexpr uint32_t BUCKET_MS          = 20;
constexpr uint32_t SAMPLES_PER_BUCKET = SAMPLE_RATE_HZ * BUCKET_MS / 1000;
constexpr size_t   RING_BUCKETS       = 640;  // 12.8 s
constexpr size_t   MAX_PER_READ       = 250;  // buckets per GET /api/live (5 s)
constexpr size_t   OLED_COLUMNS       = 128;
constexpr size_t   BUCKETS_PER_COLUMN = 5;    // 100 ms per OLED pixel column
constexpr size_t   NOW_BUCKETS        = 5;    // the big number averages the last 100 ms

struct Bucket {
    uint16_t min, max, mean;  // raw ADC counts
};

class Accumulator {
public:
    // Adds one raw sample; true when a bucket is complete (then call take()).
    bool   add(uint16_t v);
    Bucket take();

private:
    uint16_t min_ = UINT16_MAX;
    uint16_t max_ = 0;
    uint32_t sum_ = 0;
    uint32_t n_   = 0;
};

class Ring {
public:
    void     push(const Bucket& b);  // takes the next sequence number
    void     clear();                // forget held buckets; numbering continues
    uint64_t next_seq() const { return next_; }
    uint64_t first_seq() const;      // oldest bucket still held
    size_t   held() const { return static_cast<size_t>(next_ - first_seq()); }

    // Copies up to `max` buckets starting at `since`. A `since` that's no
    // longer (or never was) held starts at the oldest held bucket instead.
    // Returns the count; *first is the sequence number of out[0].
    size_t read(uint64_t since, Bucket* out, size_t max, uint64_t* first) const;

    // The newest `max` buckets, oldest first.
    size_t latest(Bucket* out, size_t max, uint64_t* first) const;

private:
    Bucket   buf_[RING_BUCKETS] = {};
    uint64_t next_  = 0;
    uint64_t start_ = 0;  // sequence number at the last clear()
};

// GET /api/live body
void write_json(JsonWriter& w, uint64_t first_seq, const Bucket* b, size_t n, uint64_t next_seq,
                uint32_t overruns);

// What the OLED draws, in display units (mV, or raw counts without calibration)
struct View {
    bool     valid = false;  // false until the first bucket arrives
    bool     millivolts = false;  // else raw counts (no calibration)
    uint16_t now   = 0;      // mean of the last NOW_BUCKETS
    uint16_t peak  = 0;      // max over the whole window
    size_t   columns = 0;    // newest at the right
    uint16_t col_min[OLED_COLUMNS] = {};
    uint16_t col_max[OLED_COLUMNS] = {};
};

// Reduces `n` buckets (oldest first, raw counts) to a View; convert() maps a
// raw count to display units.
void make_view(const Bucket* b, size_t n, uint16_t (*convert)(uint16_t raw), View* v);

}  // namespace live

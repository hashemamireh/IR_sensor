#include "live.h"

#include <algorithm>

namespace live {

bool Accumulator::add(uint16_t v) {
    min_ = std::min(min_, v);
    max_ = std::max(max_, v);
    sum_ += v;
    return ++n_ >= SAMPLES_PER_BUCKET;
}

Bucket Accumulator::take() {
    Bucket b{min_, max_, static_cast<uint16_t>(n_ ? (sum_ + n_ / 2) / n_ : 0)};
    *this = Accumulator{};
    return b;
}

void Ring::push(const Bucket& b) {
    buf_[next_ % RING_BUCKETS] = b;
    next_++;
}

void Ring::clear() { start_ = next_; }

uint64_t Ring::first_seq() const {
    uint64_t oldest_kept = next_ > RING_BUCKETS ? next_ - RING_BUCKETS : 0;
    return std::max(start_, oldest_kept);
}

size_t Ring::read(uint64_t since, Bucket* out, size_t max, uint64_t* first) const {
    uint64_t lo = first_seq();
    if (since < lo || since > next_) since = lo;
    size_t n = static_cast<size_t>(std::min<uint64_t>(next_ - since, max));
    for (size_t i = 0; i < n; i++) out[i] = buf_[(since + i) % RING_BUCKETS];
    *first = since;
    return n;
}

size_t Ring::latest(Bucket* out, size_t max, uint64_t* first) const {
    size_t   n     = std::min(held(), max);
    uint64_t since = next_ - n;
    return read(since, out, n, first);
}

void write_json(JsonWriter& w, uint64_t first_seq, const Bucket* b, size_t n, uint64_t next_seq,
                uint32_t overruns) {
    w.begin_object()
        .key("bucket_ms").num(BUCKET_MS)
        .key("first_seq").num(static_cast<int64_t>(first_seq))
        .key("next_seq").num(static_cast<int64_t>(next_seq))
        .key("overruns").num(overruns)
        .key("buckets").begin_array();
    for (size_t i = 0; i < n; i++) {
        w.begin_array().num(b[i].min).num(b[i].max).num(b[i].mean).end_array();
    }
    w.end_array().end_object();
}

void make_view(const Bucket* b, size_t n, uint16_t (*convert)(uint16_t raw), View* v) {
    *v = View{};
    if (n == 0) return;
    v->valid = true;

    // Big number: mean of the last NOW_BUCKETS
    size_t   k   = std::min(n, NOW_BUCKETS);
    uint32_t sum = 0;
    for (size_t i = n - k; i < n; i++) sum += b[i].mean;
    v->now = convert(static_cast<uint16_t>((sum + k / 2) / k));

    // Columns from the newest buckets backwards, so the right edge is "now"
    size_t cols = std::min(OLED_COLUMNS, (n + BUCKETS_PER_COLUMN - 1) / BUCKETS_PER_COLUMN);
    uint16_t peak_raw = 0;
    for (size_t c = 0; c < cols; c++) {
        size_t   end = n - c * BUCKETS_PER_COLUMN;  // exclusive
        size_t   beg = end > BUCKETS_PER_COLUMN ? end - BUCKETS_PER_COLUMN : 0;
        uint16_t lo  = UINT16_MAX, hi = 0;
        for (size_t i = beg; i < end; i++) {
            lo = std::min(lo, b[i].min);
            hi = std::max(hi, b[i].max);
        }
        size_t col = cols - 1 - c;
        v->col_min[col] = convert(lo);
        v->col_max[col] = convert(hi);
        peak_raw = std::max(peak_raw, hi);
    }
    v->columns = cols;
    v->peak    = convert(peak_raw);
}

}  // namespace live

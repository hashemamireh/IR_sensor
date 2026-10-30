#include <string>
#include <unity.h>

#include "live.h"

using live::Bucket;

void setUp() {}
void tearDown() {}

static void test_accumulator_min_max_mean() {
    live::Accumulator acc;
    bool done = false;
    for (uint32_t i = 0; i < live::SAMPLES_PER_BUCKET; i++) {
        // flat 100 with one 3000 spike — the spike must survive as the max
        done = acc.add(i == 7 ? 3000 : 100);
        TEST_ASSERT_EQUAL(i == live::SAMPLES_PER_BUCKET - 1, done);
    }
    Bucket b = acc.take();
    TEST_ASSERT_EQUAL_UINT16(100, b.min);
    TEST_ASSERT_EQUAL_UINT16(3000, b.max);
    TEST_ASSERT_EQUAL_UINT16((100 * (live::SAMPLES_PER_BUCKET - 1) + 3000 + live::SAMPLES_PER_BUCKET / 2) /
                                 live::SAMPLES_PER_BUCKET, b.mean);
    // Next bucket starts fresh
    acc.add(5);
    for (uint32_t i = 1; i < live::SAMPLES_PER_BUCKET; i++) acc.add(5);
    TEST_ASSERT_EQUAL_UINT16(5, acc.take().max);
}

static Bucket bk(uint16_t v) { return {v, v, v}; }

static void test_ring_read_since() {
    live::Ring r;
    for (uint16_t i = 0; i < 10; i++) r.push(bk(i));
    Bucket out[20];
    uint64_t first = 0;
    TEST_ASSERT_EQUAL(4, r.read(6, out, 20, &first));
    TEST_ASSERT_TRUE(first == 6);
    TEST_ASSERT_EQUAL_UINT16(6, out[0].mean);
    TEST_ASSERT_EQUAL(0, r.read(10, out, 20, &first));  // up to date
    TEST_ASSERT_EQUAL(3, r.read(0, out, 3, &first));    // capped by max
    TEST_ASSERT_TRUE(r.next_seq() == 10);
}

static void test_ring_wraps_and_clamps_old_since() {
    live::Ring r;
    for (size_t i = 0; i < live::RING_BUCKETS + 100; i++) r.push(bk(static_cast<uint16_t>(i)));
    TEST_ASSERT_TRUE(r.first_seq() == 100);
    Bucket out[live::MAX_PER_READ];
    uint64_t first = 0;
    size_t n = r.read(5, out, live::MAX_PER_READ, &first);  // 5 was overwritten
    TEST_ASSERT_TRUE(first == 100);
    TEST_ASSERT_EQUAL(live::MAX_PER_READ, n);
    TEST_ASSERT_EQUAL_UINT16(100, out[0].mean);
}

static void test_ring_clear_keeps_numbering() {
    live::Ring r;
    for (uint16_t i = 0; i < 5; i++) r.push(bk(i));
    r.clear();
    TEST_ASSERT_EQUAL(0, r.held());
    r.push(bk(42));
    Bucket out[4];
    uint64_t first = 0;
    TEST_ASSERT_EQUAL(1, r.read(2, out, 4, &first));  // a stale `since` from the old session
    TEST_ASSERT_TRUE(first == 5);
    TEST_ASSERT_EQUAL_UINT16(42, out[0].mean);
}

static void test_ring_latest() {
    live::Ring r;
    for (uint16_t i = 0; i < 8; i++) r.push(bk(i));
    Bucket out[3];
    uint64_t first = 0;
    TEST_ASSERT_EQUAL(3, r.latest(out, 3, &first));
    TEST_ASSERT_TRUE(first == 5);
    TEST_ASSERT_EQUAL_UINT16(7, out[2].mean);
}

static void test_json() {
    Bucket b[2] = {{1, 9, 4}, {2, 3000, 20}};
    char buf[160];
    JsonWriter w(buf, sizeof(buf));
    live::write_json(w, 41, b, 2, 43, 1);
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL_STRING(
        "{\"bucket_ms\":20,\"first_seq\":41,\"next_seq\":43,\"overruns\":1,"
        "\"buckets\":[[1,9,4],[2,3000,20]]}",
        buf);
}

static uint16_t half(uint16_t raw) { return raw / 2; }

static void test_view() {
    live::View v;
    live::make_view(nullptr, 0, half, &v);
    TEST_ASSERT_FALSE(v.valid);

    // 12 buckets → 3 columns, grouped from the newest: [7..11] [2..6] [0..1],
    // so the partial column is the oldest, at the left
    Bucket b[12];
    for (int i = 0; i < 12; i++) b[i] = {100, 100, 100};
    b[1]  = {100, 1000, 200};  // spike in the oldest column
    b[11] = {80, 120, 100};
    live::make_view(b, 12, half, &v);
    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_EQUAL(3, v.columns);
    TEST_ASSERT_EQUAL_UINT16(500, v.col_max[0]);  // the spike, converted
    TEST_ASSERT_EQUAL_UINT16(60, v.col_max[2]);
    TEST_ASSERT_EQUAL_UINT16(40, v.col_min[2]);
    TEST_ASSERT_EQUAL_UINT16(50, v.now);
    TEST_ASSERT_EQUAL_UINT16(500, v.peak);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_accumulator_min_max_mean);
    RUN_TEST(test_ring_read_since);
    RUN_TEST(test_ring_wraps_and_clamps_old_since);
    RUN_TEST(test_ring_clear_keeps_numbering);
    RUN_TEST(test_ring_latest);
    RUN_TEST(test_json);
    RUN_TEST(test_view);
    return UNITY_END();
}

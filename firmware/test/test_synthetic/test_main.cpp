#include <initializer_list>
#include <unity.h>

#include "synthetic.h"

void setUp() {}
void tearDown() {}

static bool near(uint16_t v, uint16_t level) {
    return v >= level - synthetic::NOISE && v <= level + synthetic::NOISE;
}

static void test_pulse_and_baseline() {
    const uint32_t rate = 10000;
    TEST_ASSERT_TRUE(near(synthetic::sample(0, rate), synthetic::PULSE_LEVEL));     // t = 0
    TEST_ASSERT_TRUE(near(synthetic::sample(49, rate), synthetic::PULSE_LEVEL));    // t = 4.9 ms
    TEST_ASSERT_TRUE(near(synthetic::sample(50, rate), synthetic::BASELINE));       // t = 5 ms
    TEST_ASSERT_TRUE(near(synthetic::sample(1000, rate), synthetic::BASELINE));     // t = 100 ms
    TEST_ASSERT_TRUE(near(synthetic::sample(2500, rate), synthetic::PULSE_LEVEL));  // t = 250 ms
}

static void test_same_timing_at_every_rate() {
    // 100 ms in is baseline, 250 ms in is a pulse — regardless of rate
    for (uint32_t rate : {1000u, 5000u, 10000u, 20000u}) {
        TEST_ASSERT_TRUE(near(synthetic::sample(rate / 10, rate), synthetic::BASELINE));
        TEST_ASSERT_TRUE(near(synthetic::sample(rate / 4, rate), synthetic::PULSE_LEVEL));
    }
}

static void test_values_fit_12_bits_and_are_deterministic() {
    for (uint64_t i = 0; i < 100000; i++) {
        uint16_t v = synthetic::sample(i, 20000);
        TEST_ASSERT_TRUE(v <= 4095);
        TEST_ASSERT_EQUAL_UINT16(v, synthetic::sample(i, 20000));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_pulse_and_baseline);
    RUN_TEST(test_same_timing_at_every_rate);
    RUN_TEST(test_values_fit_12_bits_and_are_deterministic);
    return UNITY_END();
}

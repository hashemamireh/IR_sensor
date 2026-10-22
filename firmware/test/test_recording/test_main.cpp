#include <unity.h>

#include "recording.h"

using recording::Error;

void setUp() {}
void tearDown() {}

static constexpr uint64_t PLENTY = 30ULL * 1024 * 1024;

static void test_valid_rates() {
    TEST_ASSERT_TRUE(recording::is_valid_rate(1000));
    TEST_ASSERT_TRUE(recording::is_valid_rate(20000));
    TEST_ASSERT_FALSE(recording::is_valid_rate(0));
    TEST_ASSERT_FALSE(recording::is_valid_rate(8000));
}

static void test_duration_caps() {
    TEST_ASSERT_EQUAL_UINT32(300, recording::max_duration_s(10000));
    TEST_ASSERT_EQUAL_UINT32(120, recording::max_duration_s(20000));
    TEST_ASSERT_EQUAL_UINT32(0, recording::max_duration_s(1234));
}

static void test_validate() {
    TEST_ASSERT_TRUE(recording::validate(10000, 300, PLENTY) == Error::None);
    TEST_ASSERT_TRUE(recording::validate(20000, 120, PLENTY) == Error::None);
    TEST_ASSERT_TRUE(recording::validate(20000, 121, PLENTY) == Error::InvalidDuration);
    TEST_ASSERT_TRUE(recording::validate(10000, 301, PLENTY) == Error::InvalidDuration);
    TEST_ASSERT_TRUE(recording::validate(10000, 0, PLENTY) == Error::InvalidDuration);
    TEST_ASSERT_TRUE(recording::validate(7000, 10, PLENTY) == Error::InvalidRate);
}

static void test_validate_free_space_includes_margin() {
    uint64_t need = recording::required_bytes(10000, 30);
    TEST_ASSERT_TRUE(need == 32 + 600000);
    TEST_ASSERT_TRUE(recording::validate(10000, 30, need) == Error::InsufficientSpace);
    TEST_ASSERT_TRUE(recording::validate(10000, 30, need + recording::FS_MARGIN_BYTES) == Error::None);
}

static void test_actual_rate_defaults_to_nominal() {
    TEST_ASSERT_EQUAL_UINT32(10000000, recording::actual_rate_mhz(10000));
    TEST_ASSERT_EQUAL_UINT32(0, recording::actual_rate_mhz(42));
}

static void test_path_for_id() {
    char buf[64];
    TEST_ASSERT_TRUE(recording::path_for_id(42, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("/data/recordings/00042.irec", buf);
    TEST_ASSERT_TRUE(recording::path_for_id(99999, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("/data/recordings/99999.irec", buf);
    TEST_ASSERT_FALSE(recording::path_for_id(100000, buf, sizeof(buf)));
    TEST_ASSERT_FALSE(recording::path_for_id(1, buf, 10));  // too small
}

static void test_parse_filename() {
    uint32_t id = 0;
    TEST_ASSERT_TRUE(recording::parse_filename("00042.irec", &id));
    TEST_ASSERT_EQUAL_UINT32(42, id);
    TEST_ASSERT_FALSE(recording::parse_filename("42.irec", &id));
    TEST_ASSERT_FALSE(recording::parse_filename("00042.irec.tmp", &id));
    TEST_ASSERT_FALSE(recording::parse_filename("0004a.irec", &id));
    TEST_ASSERT_FALSE(recording::parse_filename("00042.txt0", &id));
}

static void test_parse_id() {
    uint32_t id = 0;
    TEST_ASSERT_TRUE(recording::parse_id("42", &id));
    TEST_ASSERT_EQUAL_UINT32(42, id);
    TEST_ASSERT_TRUE(recording::parse_id("00042", &id));
    TEST_ASSERT_EQUAL_UINT32(42, id);
    TEST_ASSERT_FALSE(recording::parse_id("", &id));
    TEST_ASSERT_FALSE(recording::parse_id("123456", &id));
    TEST_ASSERT_FALSE(recording::parse_id("-1", &id));
    TEST_ASSERT_FALSE(recording::parse_id("4 2", &id));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_rates);
    RUN_TEST(test_duration_caps);
    RUN_TEST(test_validate);
    RUN_TEST(test_validate_free_space_includes_margin);
    RUN_TEST(test_actual_rate_defaults_to_nominal);
    RUN_TEST(test_path_for_id);
    RUN_TEST(test_parse_filename);
    RUN_TEST(test_parse_id);
    return UNITY_END();
}

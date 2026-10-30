#include <initializer_list>
#include <unity.h>

#include "settings.h"

using settings::Brightness;
using settings::Settings;

void setUp() {}
void tearDown() {}

static void test_defaults_are_valid() {
    Settings s;
    TEST_ASSERT_TRUE(settings::is_valid(s));
    TEST_ASSERT_EQUAL_UINT32(60, s.screen_timeout_s);
    TEST_ASSERT_EQUAL_UINT32(10000, s.default_rate_hz);
    TEST_ASSERT_EQUAL_UINT32(30, s.default_duration_s);
    TEST_ASSERT_EQUAL_UINT32(900, s.sleep_timeout_s);
}

static void test_brightness_roundtrip() {
    Brightness b;
    for (Brightness in : {Brightness::Low, Brightness::Medium, Brightness::High}) {
        TEST_ASSERT_TRUE(settings::parse_brightness(settings::brightness_str(in), &b));
        TEST_ASSERT_TRUE(b == in);
    }
    TEST_ASSERT_FALSE(settings::parse_brightness("HIGH", &b));
    TEST_ASSERT_FALSE(settings::parse_brightness("", &b));
}

static void test_field_validation() {
    TEST_ASSERT_TRUE(settings::is_valid_screen_timeout(0));  // never
    TEST_ASSERT_TRUE(settings::is_valid_screen_timeout(300));
    TEST_ASSERT_FALSE(settings::is_valid_screen_timeout(45));
    TEST_ASSERT_TRUE(settings::is_valid_default_duration(5));
    TEST_ASSERT_FALSE(settings::is_valid_default_duration(120));
    TEST_ASSERT_TRUE(settings::is_valid_sleep_timeout(0));  // never
    TEST_ASSERT_TRUE(settings::is_valid_sleep_timeout(1800));
    TEST_ASSERT_FALSE(settings::is_valid_sleep_timeout(60));
}

static void test_is_valid_catches_corrupt_values() {
    Settings s;
    s.brightness = static_cast<Brightness>(7);
    TEST_ASSERT_FALSE(settings::is_valid(s));

    s = Settings{};
    s.default_rate_hz = 8000;
    TEST_ASSERT_FALSE(settings::is_valid(s));

    s = Settings{};
    s.sleep_timeout_s = 42;
    TEST_ASSERT_FALSE(settings::is_valid(s));
}

static void test_json() {
    char buf[192];
    JsonWriter w(buf, sizeof(buf));
    settings::write_json(w, Settings{});
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL_STRING(
        "{\"brightness\":\"medium\",\"screen_timeout_s\":60,"
        "\"default_sample_rate_hz\":10000,\"default_duration_s\":30,\"sleep_timeout_s\":900}",
        buf);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_brightness_roundtrip);
    RUN_TEST(test_field_validation);
    RUN_TEST(test_is_valid_catches_corrupt_values);
    RUN_TEST(test_json);
    return UNITY_END();
}

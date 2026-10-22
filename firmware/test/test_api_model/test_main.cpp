#include <cstring>
#include <initializer_list>
#include <unity.h>

#include "api_model.h"

using api::DeviceState;
using api::Route;

void setUp() {}
void tearDown() {}

static void test_idle_allows_everything_but_stop() {
    TEST_ASSERT_TRUE(api::route_allowed(DeviceState::Idle, Route::RecordStart));
    TEST_ASSERT_TRUE(api::route_allowed(DeviceState::Idle, Route::RecordingsDeleteAll));
    TEST_ASSERT_TRUE(api::route_allowed(DeviceState::Idle, Route::SettingsSet));
    TEST_ASSERT_FALSE(api::route_allowed(DeviceState::Idle, Route::RecordStop));
}

static void test_busy_states_allow_only_status_settings_get_and_stop() {
    for (DeviceState s : {DeviceState::Recording, DeviceState::Saving, DeviceState::Compressing}) {
        TEST_ASSERT_TRUE(api::route_allowed(s, Route::Status));
        TEST_ASSERT_TRUE(api::route_allowed(s, Route::SettingsGet));
        TEST_ASSERT_FALSE(api::route_allowed(s, Route::RecordStart));
        TEST_ASSERT_FALSE(api::route_allowed(s, Route::RecordingGet));
        TEST_ASSERT_FALSE(api::route_allowed(s, Route::SettingsSet));
        TEST_ASSERT_FALSE(api::route_allowed(s, Route::Time));
        TEST_ASSERT_FALSE(api::route_allowed(s, Route::Calibration));
    }
    TEST_ASSERT_TRUE(api::route_allowed(DeviceState::Recording, Route::RecordStop));
    TEST_ASSERT_FALSE(api::route_allowed(DeviceState::Saving, Route::RecordStop));
}

static void test_status_idle() {
    api::StatusInfo s;
    s.battery_mv = 3900;
    s.battery_pct = 78;
    s.free_bytes = 15000000;
    s.recording_count = 12;
    s.ssid = "IRSensor-AB12";
    s.client_connected = true;

    char buf[256];
    JsonWriter w(buf, sizeof(buf));
    api::write_status(w, s);
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL_STRING(
        "{\"state\":\"idle\",\"battery\":{\"mv\":3900,\"pct\":78},"
        "\"free_bytes\":15000000,\"recording_count\":12,"
        "\"wifi\":{\"ssid\":\"IRSensor-AB12\",\"client_connected\":true}}",
        buf);
}

static void test_status_recording_and_compressing() {
    api::StatusInfo s;
    s.state = DeviceState::Recording;
    s.recording_id = 42;
    s.elapsed_s = 5;
    s.total_s = 30;

    char buf[256];
    JsonWriter w(buf, sizeof(buf));
    api::write_status(w, s);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"state\":\"recording\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"recording\":{\"id\":42,\"elapsed_s\":5,\"total_s\":30}}"));

    s.state = DeviceState::Compressing;
    s.compress_done = 3;
    s.compress_total = 10;
    JsonWriter w2(buf, sizeof(buf));
    api::write_status(w2, s);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"compress\":{\"done\":3,\"total\":10}}"));
    TEST_ASSERT_NULL(strstr(buf, "\"recording\":"));
}

static void test_recording_entry() {
    irec::Header h;
    h.sample_rate_mhz = 10000000;
    h.sample_count = 300000;
    h.start_utc_us = 1792540800000000ULL;
    h.flags = irec::FLAG_COMPRESSED;

    char buf[256];
    JsonWriter w(buf, sizeof(buf));
    api::write_recording(w, 42, h, 123456);
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL_STRING(
        "{\"id\":42,\"start_utc_us\":1792540800000000,\"duration_ms\":30000,"
        "\"sample_rate_mhz\":10000000,\"sample_count\":300000,"
        "\"size_bytes\":123456,\"compressed\":true,\"overrun\":false}",
        buf);
}

static void test_busy_body() {
    char buf[64];
    JsonWriter w(buf, sizeof(buf));
    api::write_busy(w, DeviceState::Saving);
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"busy\",\"state\":\"saving\"}", buf);
}

static void test_error_body() {
    char buf[96];
    JsonWriter w(buf, sizeof(buf));
    api::write_error(w, "not_found");
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"not_found\"}", buf);

    JsonWriter w2(buf, sizeof(buf));
    api::write_error(w2, "invalid_setting", "brightness");
    TEST_ASSERT_EQUAL_STRING("{\"error\":\"invalid_setting\",\"field\":\"brightness\"}", buf);
}

static void test_plausible_utc() {
    TEST_ASSERT_TRUE(api::is_plausible_utc_us(1792540800000000LL));   // 2026-10
    TEST_ASSERT_FALSE(api::is_plausible_utc_us(0));
    TEST_ASSERT_FALSE(api::is_plausible_utc_us(1792540800LL));        // seconds, not µs
    TEST_ASSERT_FALSE(api::is_plausible_utc_us(4102444800LL * 1000000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_allows_everything_but_stop);
    RUN_TEST(test_busy_states_allow_only_status_settings_get_and_stop);
    RUN_TEST(test_status_idle);
    RUN_TEST(test_status_recording_and_compressing);
    RUN_TEST(test_recording_entry);
    RUN_TEST(test_busy_body);
    RUN_TEST(test_error_body);
    RUN_TEST(test_plausible_utc);
    return UNITY_END();
}

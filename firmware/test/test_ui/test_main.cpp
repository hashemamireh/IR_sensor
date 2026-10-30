#include <cstdio>
#include <unity.h>

#include "button.h"
#include "framebuffer.h"
#include "led_color.h"

using ui::ButtonDetector;
using ui::ButtonEvent;
using ui::Framebuffer;

void setUp() {}
void tearDown() {}

// ── Framebuffer ──

static void test_page_layout() {
    Framebuffer fb;
    fb.set_pixel(5, 9, true);  // page 1, bit 1
    TEST_ASSERT_EQUAL_HEX8(0x02, fb.data()[1 * Framebuffer::WIDTH + 5]);
    TEST_ASSERT_TRUE(fb.pixel(5, 9));
    fb.set_pixel(5, 9, false);
    TEST_ASSERT_EQUAL_HEX8(0x00, fb.data()[1 * Framebuffer::WIDTH + 5]);
}

static void test_clipping() {
    Framebuffer fb;
    fb.set_pixel(-1, 0, true);
    fb.set_pixel(128, 0, true);
    fb.set_pixel(0, 64, true);
    fb.fill_rect(120, 60, 20, 20, true);  // partly off-screen
    TEST_ASSERT_TRUE(fb.pixel(127, 63));
    TEST_ASSERT_FALSE(fb.pixel(-1, 0));
}

static void test_glyph_shape() {
    Framebuffer fb;
    TEST_ASSERT_EQUAL_INT(12, fb.text(0, 0, "A1"));
    // 'A': rows 1–6 of the left column, top bar on row 0 (columns 1–3)
    TEST_ASSERT_FALSE(fb.pixel(0, 0));
    TEST_ASSERT_TRUE(fb.pixel(1, 0));
    TEST_ASSERT_TRUE(fb.pixel(0, 6));
    TEST_ASSERT_TRUE(fb.pixel(4, 6));
    TEST_ASSERT_TRUE(fb.pixel(2, 4));  // crossbar
    TEST_ASSERT_FALSE(fb.pixel(5, 3)); // spacing column
    // '1': vertical stroke in its middle column
    for (int y = 0; y < 7; y++) TEST_ASSERT_TRUE(fb.pixel(8, y));
}

static void test_scaled_text() {
    Framebuffer fb;
    TEST_ASSERT_EQUAL_INT(12, fb.text(0, 0, "1", true, 2));
    TEST_ASSERT_EQUAL_INT(24, Framebuffer::text_width("12", 2));
    // '1' stroke is column 2 at scale 1 → columns 4–5 at scale 2, rows 0–13
    TEST_ASSERT_TRUE(fb.pixel(4, 0));
    TEST_ASSERT_TRUE(fb.pixel(5, 13));
    TEST_ASSERT_FALSE(fb.pixel(4, 14));
}

static void test_inverted_text() {
    Framebuffer fb;
    fb.fill_rect(0, 0, 6, 8, true);
    fb.text(0, 0, "A", false);
    TEST_ASSERT_FALSE(fb.pixel(1, 0));  // ink cleared
    TEST_ASSERT_TRUE(fb.pixel(0, 0));   // background kept
}

// ── Button ──

static void test_short_press_fires_on_release() {
    ButtonDetector b;
    TEST_ASSERT_TRUE(b.update(true, 0) == ButtonEvent::None);
    TEST_ASSERT_TRUE(b.update(true, 40) == ButtonEvent::None);  // debounced press
    TEST_ASSERT_TRUE(b.update(false, 200) == ButtonEvent::None);
    TEST_ASSERT_TRUE(b.update(false, 240) == ButtonEvent::Press);
    TEST_ASSERT_TRUE(b.update(false, 300) == ButtonEvent::None);
}

static void test_bounce_is_ignored() {
    ButtonDetector b;
    for (uint32_t t = 0; t < 20; t += 2) {
        TEST_ASSERT_TRUE(b.update(t % 4 == 0, t) == ButtonEvent::None);
    }
    TEST_ASSERT_TRUE(b.update(false, 100) == ButtonEvent::None);
}

static void test_long_press_fires_once_while_held() {
    ButtonDetector b;
    b.update(true, 0);
    b.update(true, 40);  // pressed_at = 40
    TEST_ASSERT_TRUE(b.update(true, 1039) == ButtonEvent::None);
    TEST_ASSERT_TRUE(b.update(true, 1040) == ButtonEvent::LongPress);
    TEST_ASSERT_TRUE(b.update(true, 3000) == ButtonEvent::None);
    b.update(false, 3100);
    TEST_ASSERT_TRUE(b.update(false, 3200) == ButtonEvent::None);  // no Press after a long press
}

// ── LED ──

static void test_led_colors() {
    using api::DeviceState;
    ui::Rgb green = ui::led_color(DeviceState::Idle, false, false, 0);
    TEST_ASSERT_TRUE(green.g > 0 && green.r == 0 && green.b == 0);

    ui::Rgb cyan = ui::led_color(DeviceState::Idle, true, false, 0);
    TEST_ASSERT_TRUE(cyan.g > 0 && cyan.b > 0 && cyan.r == 0);

    ui::Rgb amber = ui::led_color(DeviceState::Idle, true, true, 0);  // low battery wins
    TEST_ASSERT_TRUE(amber.r > amber.g && amber.g > 0 && amber.b == 0);

    ui::Rgb dim    = ui::led_color(DeviceState::Recording, true, true, 0);
    ui::Rgb bright = ui::led_color(DeviceState::Recording, true, true, ui::PULSE_PERIOD_MS / 2);
    TEST_ASSERT_TRUE(dim.g == 0 && dim.b == 0 && bright.g == 0 && bright.b == 0);
    TEST_ASSERT_TRUE(bright.r > dim.r);  // pulsing red

    ui::Rgb live_dim    = ui::led_color(DeviceState::Live, false, false, 0);
    ui::Rgb live_bright = ui::led_color(DeviceState::Live, false, false, ui::PULSE_PERIOD_MS / 2);
    TEST_ASSERT_TRUE(live_bright.g > live_dim.g && live_bright.r == 0);  // pulsing green
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_page_layout);
    RUN_TEST(test_clipping);
    RUN_TEST(test_glyph_shape);
    RUN_TEST(test_scaled_text);
    RUN_TEST(test_inverted_text);
    RUN_TEST(test_short_press_fires_on_release);
    RUN_TEST(test_bounce_is_ignored);
    RUN_TEST(test_long_press_fires_once_while_held);
    RUN_TEST(test_led_colors);
    return UNITY_END();
}

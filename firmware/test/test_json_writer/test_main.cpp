#include <string>
#include <unity.h>

#include "json_writer.h"

void setUp() {}
void tearDown() {}

static void test_nested_object_and_array() {
    char buf[128];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object()
        .key("a").num(1)
        .key("b").begin_array().num(-2).boolean(true).null().str("x").end_array()
        .key("c").begin_object().end_object()
     .end_object();
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":[-2,true,null,\"x\"],\"c\":{}}", buf);
}

static void test_large_numbers() {
    char buf[64];
    JsonWriter w(buf, sizeof(buf));
    w.begin_array().num(1792540800123456LL).num(INT64_MIN).end_array();
    TEST_ASSERT_EQUAL_STRING("[1792540800123456,-9223372036854775808]", buf);
}

static void test_string_escaping() {
    char buf[64];
    JsonWriter w(buf, sizeof(buf));
    w.str("q\"b\\n\nt\t\x01");
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL_STRING("\"q\\\"b\\\\n\\nt\\t\\u0001\"", buf);
}

static void test_overflow_sets_not_ok_and_stays_terminated() {
    char buf[8];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object().key("long_key").num(1).end_object();
    TEST_ASSERT_FALSE(w.ok());
    TEST_ASSERT_TRUE(w.length() < sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[w.length()]);
}

static void test_exact_fit() {
    char buf[3];  // "[]" + terminator
    JsonWriter w(buf, sizeof(buf));
    w.begin_array().end_array();
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL_STRING("[]", buf);
}

static void test_streaming_with_clear_buffer() {
    char buf[32];
    JsonWriter w(buf, sizeof(buf));
    std::string out;

    w.begin_array();
    for (int i = 0; i < 3; i++) {
        w.begin_object().key("id").num(i).end_object();
        out.append(w.c_str(), w.length());
        w.clear_buffer();
    }
    w.end_array();
    out.append(w.c_str(), w.length());

    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL_STRING("[{\"id\":0},{\"id\":1},{\"id\":2}]", out.c_str());
}

static void test_unbalanced_close_sets_not_ok() {
    char buf[16];
    JsonWriter w(buf, sizeof(buf));
    w.end_object();
    TEST_ASSERT_FALSE(w.ok());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nested_object_and_array);
    RUN_TEST(test_large_numbers);
    RUN_TEST(test_string_escaping);
    RUN_TEST(test_overflow_sets_not_ok_and_stays_terminated);
    RUN_TEST(test_exact_fit);
    RUN_TEST(test_streaming_with_clear_buffer);
    RUN_TEST(test_unbalanced_close_sets_not_ok);
    return UNITY_END();
}

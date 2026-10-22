#include <cstring>
#include <unity.h>

#include "irec.h"

void setUp() {}
void tearDown() {}

static void test_crc32_known_vector() {
    // Standard CRC-32 check value
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, irec::crc32_update(0, "123456789", 9));
}

static void test_crc32_empty_is_zero() {
    TEST_ASSERT_EQUAL_HEX32(0, irec::crc32_update(0, "", 0));
}

static void test_crc32_chunked_matches_whole() {
    uint8_t data[1000];
    for (int i = 0; i < 1000; i++) data[i] = static_cast<uint8_t>(i * 7 + 3);

    uint32_t whole = irec::crc32_update(0, data, sizeof(data));
    uint32_t crc = 0;
    crc = irec::crc32_update(crc, data, 1);
    crc = irec::crc32_update(crc, data + 1, 500);
    crc = irec::crc32_update(crc, data + 501, 499);
    TEST_ASSERT_EQUAL_HEX32(whole, crc);
}

static void test_header_layout_matches_spec() {
    irec::Header h;
    h.sample_rate_mhz = 0x11223344;
    h.sample_count    = 0x55667788;
    h.start_utc_us    = 0x0102030405060708ULL;
    h.crc32           = 0xAABBCCDD;
    h.flags           = irec::FLAG_COMPRESSED;

    uint8_t buf[irec::HEADER_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    irec::encode_header(h, buf);

    const uint8_t expected[irec::HEADER_SIZE] = {
        'I', 'R', 'E', 'C',                              // 0x00 magic
        0x01,                                            // 0x04 version
        0x44, 0x33, 0x22, 0x11,                          // 0x05 rate (mHz)
        0x88, 0x77, 0x66, 0x55,                          // 0x09 sample count
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,  // 0x0D start timestamp
        0xDD, 0xCC, 0xBB, 0xAA,                          // 0x15 CRC
        0x01,                                            // 0x19 flags
        0, 0, 0, 0, 0, 0,                                // 0x1A reserved
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, irec::HEADER_SIZE);
}

static void test_header_roundtrip() {
    irec::Header in;
    in.sample_rate_mhz = 10004300;
    in.sample_count    = 3000000;
    in.start_utc_us    = 1792540800123456ULL;
    in.crc32           = 0xDEADBEEF;
    in.flags           = 0;

    uint8_t buf[irec::HEADER_SIZE];
    irec::encode_header(in, buf);

    irec::Header out;
    TEST_ASSERT_TRUE(irec::decode_header(buf, &out));
    TEST_ASSERT_EQUAL_UINT32(in.sample_rate_mhz, out.sample_rate_mhz);
    TEST_ASSERT_EQUAL_UINT32(in.sample_count, out.sample_count);
    TEST_ASSERT_TRUE(in.start_utc_us == out.start_utc_us);
    TEST_ASSERT_EQUAL_HEX32(in.crc32, out.crc32);
    TEST_ASSERT_EQUAL_UINT8(in.flags, out.flags);
}

static void test_decode_rejects_bad_magic_and_version() {
    uint8_t buf[irec::HEADER_SIZE];
    irec::encode_header(irec::Header{}, buf);
    irec::Header out;

    buf[0] = 'X';
    TEST_ASSERT_FALSE(irec::decode_header(buf, &out));

    irec::encode_header(irec::Header{}, buf);
    buf[4] = 0x02;
    TEST_ASSERT_FALSE(irec::decode_header(buf, &out));
}

static void test_duration_ms() {
    irec::Header h;
    h.sample_rate_mhz = 10000000;  // 10 kHz
    h.sample_count    = 300000;
    TEST_ASSERT_TRUE(irec::duration_ms(h) == 30000);

    h.sample_rate_mhz = 0;
    TEST_ASSERT_TRUE(irec::duration_ms(h) == 0);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_crc32_empty_is_zero);
    RUN_TEST(test_crc32_chunked_matches_whole);
    RUN_TEST(test_header_layout_matches_spec);
    RUN_TEST(test_header_roundtrip);
    RUN_TEST(test_decode_rejects_bad_magic_and_version);
    RUN_TEST(test_duration_ms);
    return UNITY_END();
}

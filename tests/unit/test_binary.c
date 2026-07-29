#include "test.h"

#include "common/binary.h"

#include <string.h>

static bool test_binary_big_endian_layout(void) {
    uint8_t      buffer[15] = {0};
    BinaryWriter writer;

    binary_writer_init(&writer, buffer, sizeof(buffer));

    TEST_ASSERT(binary_write_u8(&writer, 0xABU));
    TEST_ASSERT(binary_write_u16(&writer, 0x1234U));
    TEST_ASSERT(binary_write_u32(&writer, 0x55667788U));
    TEST_ASSERT(binary_write_u64(&writer, UINT64_C(0x0102030405060708)));
    TEST_ASSERT(binary_writer_size(&writer) == sizeof(buffer));
    TEST_ASSERT(binary_writer_remaining(&writer) == 0U);

    static const uint8_t expected[] = {
        0xABU,
        0x12U,
        0x34U,
        0x55U,
        0x66U,
        0x77U,
        0x88U,
        0x01U,
        0x02U,
        0x03U,
        0x04U,
        0x05U,
        0x06U,
        0x07U,
        0x08U,
    };

    TEST_ASSERT(memcmp(buffer, expected, sizeof(expected)) == 0);

    return true;
}

static bool test_binary_round_trip(void) {
    uint8_t              buffer[64] = {0};
    static const uint8_t bytes[]    = {0x00U, 0x7FU, 0x80U, 0xFFU};
    BinaryWriter         writer;

    binary_writer_init(&writer, buffer, sizeof(buffer));

    TEST_ASSERT(binary_write_u8(&writer, 0xA5U));
    TEST_ASSERT(binary_write_u16(&writer, 0xBEEFU));
    TEST_ASSERT(binary_write_u32(&writer, 0xDEADBEEFU));
    TEST_ASSERT(binary_write_u64(&writer, UINT64_C(0x1122334455667788)));
    TEST_ASSERT(binary_write_bytes(&writer, bytes, sizeof(bytes)));

    BinaryReader reader;
    binary_reader_init(&reader, buffer, binary_writer_size(&writer));

    uint8_t  value8                  = 0U;
    uint16_t value16                 = 0U;
    uint32_t value32                 = 0U;
    uint64_t value64                 = 0U;
    uint8_t  restored[sizeof(bytes)] = {0};

    TEST_ASSERT(binary_read_u8(&reader, &value8));
    TEST_ASSERT(binary_read_u16(&reader, &value16));
    TEST_ASSERT(binary_read_u32(&reader, &value32));
    TEST_ASSERT(binary_read_u64(&reader, &value64));
    TEST_ASSERT(binary_read_bytes(&reader, restored, sizeof(restored)));

    TEST_ASSERT(value8 == 0xA5U);
    TEST_ASSERT(value16 == 0xBEEFU);
    TEST_ASSERT(value32 == 0xDEADBEEFU);
    TEST_ASSERT(value64 == UINT64_C(0x1122334455667788));
    TEST_ASSERT(memcmp(restored, bytes, sizeof(bytes)) == 0);
    TEST_ASSERT(binary_reader_is_finished(&reader));
    TEST_ASSERT(binary_reader_remaining(&reader) == 0U);

    return true;
}

static bool test_binary_read_view(void) {
    static const uint8_t buffer[] = {1U, 2U, 3U, 4U, 5U};
    BinaryReader         reader;
    const uint8_t*       view = NULL;

    binary_reader_init(&reader, buffer, sizeof(buffer));

    TEST_ASSERT(binary_read_view(&reader, &view, 3U));
    TEST_ASSERT(view == buffer);
    TEST_ASSERT(memcmp(view, buffer, 3U) == 0);
    TEST_ASSERT(binary_reader_remaining(&reader) == 2U);

    TEST_ASSERT(binary_read_view(&reader, &view, 2U));
    TEST_ASSERT(view == buffer + 3U);
    TEST_ASSERT(binary_reader_is_finished(&reader));

    return true;
}

static bool test_binary_bounds(void) {
    uint8_t      buffer[4] = {0};
    BinaryWriter writer;

    binary_writer_init(&writer, buffer, sizeof(buffer));
    TEST_ASSERT(binary_write_u32(&writer, 1U));
    TEST_ASSERT(!binary_write_u8(&writer, 2U));
    TEST_ASSERT(binary_writer_size(&writer) == sizeof(buffer));

    BinaryReader reader;
    binary_reader_init(&reader, buffer, sizeof(buffer));

    uint32_t value = 0U;
    uint8_t  extra = 0U;

    TEST_ASSERT(binary_read_u32(&reader, &value));
    TEST_ASSERT(value == 1U);
    TEST_ASSERT(!binary_read_u8(&reader, &extra));
    TEST_ASSERT(binary_reader_is_finished(&reader));

    return true;
}

static bool test_binary_invalid_arguments(void) {
    uint8_t      buffer[8] = {0};
    uint32_t     value     = 0U;
    BinaryReader reader;
    BinaryWriter writer;

    binary_reader_init(&reader, buffer, sizeof(buffer));
    binary_writer_init(&writer, buffer, sizeof(buffer));

    TEST_ASSERT(!binary_read_u32(NULL, &value));
    TEST_ASSERT(!binary_read_u32(&reader, NULL));
    TEST_ASSERT(!binary_read_bytes(&reader, NULL, 1U));
    TEST_ASSERT(!binary_read_view(&reader, NULL, 1U));

    TEST_ASSERT(!binary_write_u32(NULL, value));
    TEST_ASSERT(!binary_write_bytes(&writer, NULL, 1U));

    TEST_ASSERT(binary_reader_remaining(NULL) == 0U);
    TEST_ASSERT(binary_writer_remaining(NULL) == 0U);
    TEST_ASSERT(binary_writer_size(NULL) == 0U);
    TEST_ASSERT(!binary_reader_is_finished(NULL));

    return true;
}

static bool test_binary_zero_size_operations(void) {
    BinaryReader   reader;
    BinaryWriter   writer;
    const uint8_t* view = (const uint8_t*)1;

    binary_reader_init(&reader, NULL, 0U);
    binary_writer_init(&writer, NULL, 0U);

    TEST_ASSERT(binary_read_bytes(&reader, NULL, 0U));
    TEST_ASSERT(binary_read_view(&reader, &view, 0U));
    TEST_ASSERT(view == NULL);
    TEST_ASSERT(binary_write_bytes(&writer, NULL, 0U));
    TEST_ASSERT(binary_reader_is_finished(&reader));
    TEST_ASSERT(binary_writer_size(&writer) == 0U);

    return true;
}

void register_binary_tests(TestSuite* suite) {
    TEST_ADD(suite, test_binary_big_endian_layout);
    TEST_ADD(suite, test_binary_round_trip);
    TEST_ADD(suite, test_binary_read_view);
    TEST_ADD(suite, test_binary_bounds);
    TEST_ADD(suite, test_binary_invalid_arguments);
    TEST_ADD(suite, test_binary_zero_size_operations);
}

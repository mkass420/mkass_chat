#include "test.h"

#include "server/dispatcher.h"

#include <string.h>

static ParsedFrame make_request(
    MessageType type,
    uint32_t request_id,
    const uint8_t* payload,
    uint32_t payload_len
) {
    ParsedFrame frame = {
        .header = {
            .magic            = PROTOCOL_MAGIC,
            .type             = type,
            .flags            = 0U,
            .request_id       = request_id,
            .uncompressed_len = payload_len,
            .payload_len      = payload_len,
            .payload_crc32    = 0U,
        },
        .payload = payload,
        .frame_size = PACKET_HEADER_WIRE_SIZE + (size_t)payload_len,
    };

    return frame;
}

static ParsedFrame parse_queued_frame(const ClientSession* session) {
    ParsedFrame frame = {0};
    const FrameParseResult result = frame_try_parse(session->write_buffer, session->write_bytes, &frame);

    if(result != FRAME_PARSE_COMPLETE) {
        frame.frame_size = 0U;
    }

    return frame;
}

static void test_dispatcher_ping(void) {
    int dummy_context = 1;
    ClientSession session;
    session_init(&session, 10);

    const ParsedFrame request = make_request(MSG_TYPE_PING_REQUEST, 1U, NULL, 0U);

    TEST_ASSERT_EQ_INT(
        SESSION_IO_OK, server_dispatch_frame(&dummy_context, &session, &request)
    );

    const ParsedFrame response = parse_queued_frame(&session);

    TEST_ASSERT(response.frame_size != 0U);
    TEST_ASSERT_EQ_INT(MSG_TYPE_PING_RESPONSE, response.header.type);
    TEST_ASSERT_EQ_U32(1U, response.header.request_id);
    TEST_ASSERT_EQ_U32(0U, response.header.payload_len);
}

static void test_dispatcher_echo(void) {
    int dummy_context = 1;
    static const uint8_t payload[] = "echo payload";
    ClientSession session;
    session_init(&session, 10);

    const ParsedFrame request = make_request(
        MSG_TYPE_ECHO_REQUEST, 2U, payload, (uint32_t)(sizeof(payload) - 1U)
    );

    TEST_ASSERT_EQ_INT(
        SESSION_IO_OK, server_dispatch_frame(&dummy_context, &session, &request)
    );

    const ParsedFrame response = parse_queued_frame(&session);

    TEST_ASSERT(response.frame_size != 0U);
    TEST_ASSERT_EQ_INT(MSG_TYPE_ECHO_RESPONSE, response.header.type);
    TEST_ASSERT_EQ_U32(2U, response.header.request_id);
    TEST_ASSERT_MEMORY(payload, response.payload, sizeof(payload) - 1U);
}

static void test_dispatcher_unsupported_request(void) {
    int dummy_context = 1;
    static const uint8_t request_payload[] = "registration";
    static const uint8_t expected_payload[] = "Message type is not implemented";
    ClientSession session;
    session_init(&session, 10);

    const ParsedFrame request = make_request(
        MSG_TYPE_REGISTER_REQUEST, 3U, request_payload, (uint32_t)(sizeof(request_payload) - 1U)
    );

    TEST_ASSERT_EQ_INT(
        SESSION_IO_OK, server_dispatch_frame(&dummy_context, &session, &request)
    );

    const ParsedFrame response = parse_queued_frame(&session);

    TEST_ASSERT(response.frame_size != 0U);
    TEST_ASSERT_EQ_INT(MSG_TYPE_ERROR_RESPONSE, response.header.type);
    TEST_ASSERT_EQ_U32(3U, response.header.request_id);
    TEST_ASSERT_EQ_U32(sizeof(expected_payload) - 1U, response.header.payload_len);
    TEST_ASSERT_MEMORY(expected_payload, response.payload, sizeof(expected_payload) - 1U);
}

static void test_dispatcher_rejects_server_message(void) {
    int dummy_context = 1;
    ClientSession session;
    session_init(&session, 10);

    const ParsedFrame response = make_request(MSG_TYPE_PING_RESPONSE, 4U, NULL, 0U);

    TEST_ASSERT_EQ_INT(
        SESSION_IO_PROTOCOL_ERROR, server_dispatch_frame(&dummy_context, &session, &response)
    );
    TEST_ASSERT_EQ_SIZE(0U, session.write_bytes);
}

static void test_dispatcher_propagates_queue_error(void) {
    int dummy_context = 1;
    ClientSession session;
    session_init(&session, 10);
    session.write_bytes = sizeof(session.write_buffer);

    const ParsedFrame request = make_request(MSG_TYPE_PING_REQUEST, 5U, NULL, 0U);

    TEST_ASSERT_EQ_INT(
        SESSION_IO_BUFFER_FULL, server_dispatch_frame(&dummy_context, &session, &request)
    );
}

void register_dispatcher_tests(TestSuite* suite) {
    TEST_ADD(suite, test_dispatcher_ping);
    TEST_ADD(suite, test_dispatcher_echo);
    TEST_ADD(suite, test_dispatcher_unsupported_request);
    TEST_ADD(suite, test_dispatcher_rejects_server_message);
    TEST_ADD(suite, test_dispatcher_propagates_queue_error);
}

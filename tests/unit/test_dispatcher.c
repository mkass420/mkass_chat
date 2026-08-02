#define _GNU_SOURCE

#include "test.h"

#include "common/error_protocol.h"
#include "server/dispatcher.h"

#include <string.h>
#include <sys/mman.h>

typedef struct {
    ServerState*          server;
    ClientConnection      connection;
    ServerDispatchContext dispatch;
} DispatcherFixture;

static bool dispatcher_fixture_init(DispatcherFixture* fixture) {
    if(fixture == NULL) {
        return false;
    }

    memset(fixture, 0, sizeof(*fixture));

    fixture->server = mmap(NULL, sizeof(*fixture->server), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if(fixture->server == MAP_FAILED) {
        fixture->server = NULL;
        return false;
    }

    connection_slot_init(&fixture->connection);

    connection_open(&fixture->connection, 10);

    fixture->dispatch.server = fixture->server;

    fixture->dispatch.connection = &fixture->connection;

    return true;
}

static void dispatcher_fixture_destroy(DispatcherFixture* fixture) {
    if(fixture == NULL) {
        return;
    }

    if(fixture->server != NULL) {
        (void)munmap(fixture->server, sizeof(*fixture->server));

        fixture->server = NULL;
    }
}

static DecodedFrame make_request(MessageType type, uint32_t request_id, const uint8_t* payload, uint32_t payload_len) {
    return (DecodedFrame){
        .header =
            {
                     .magic            = PROTOCOL_MAGIC,
                     .type             = type,
                     .flags            = 0U,
                     .request_id       = request_id,
                     .uncompressed_len = payload_len,
                     .payload_len      = payload_len,
                     .payload_crc32    = 0U,
                     },
        .payload     = payload,
        .payload_len = payload_len,
    };
}

static bool decode_queued_frame(DispatcherFixture* fixture, ParsedFrame* parsed, DecodedFrame* decoded) {
    if(fixture == NULL || fixture->server == NULL || parsed == NULL || decoded == NULL) {
        return false;
    }

    const TransportSession* transport = &fixture->connection.transport;

    if(frame_try_parse(transport->write_buffer, transport->write_bytes, parsed) != FRAME_PARSE_COMPLETE) {
        return false;
    }

    return frame_decode(&fixture->server->frame_codec, parsed, decoded) == FRAME_DECODE_OK;
}

static bool test_dispatcher_ping(void) {
    DispatcherFixture fixture;

    TEST_ASSERT(dispatcher_fixture_init(&fixture));

    const DecodedFrame request = make_request(MSG_TYPE_PING_REQUEST, 1U, NULL, 0U);

    const TransportIoResult result = server_dispatch_frame(&fixture.dispatch, &request);

    ParsedFrame  parsed   = {0};
    DecodedFrame response = {0};

    const bool decoded = decode_queued_frame(&fixture, &parsed, &response);

    const bool response_matches = decoded && response.header.type == MSG_TYPE_PING_RESPONSE &&
                                  response.header.request_id == 1U && response.payload_len == 0U;

    dispatcher_fixture_destroy(&fixture);

    TEST_ASSERT(result == TRANSPORT_IO_OK);

    TEST_ASSERT(decoded);
    TEST_ASSERT(response_matches);

    return true;
}

static bool test_dispatcher_echo(void) {
    DispatcherFixture fixture;

    TEST_ASSERT(dispatcher_fixture_init(&fixture));

    static const uint8_t payload[] = "echo payload";

    const uint32_t payload_len = (uint32_t)(sizeof(payload) - 1U);

    const DecodedFrame request = make_request(MSG_TYPE_ECHO_REQUEST, 2U, payload, payload_len);

    const TransportIoResult result = server_dispatch_frame(&fixture.dispatch, &request);

    ParsedFrame  parsed   = {0};
    DecodedFrame response = {0};

    const bool decoded = decode_queued_frame(&fixture, &parsed, &response);

    const bool response_matches = decoded && response.header.type == MSG_TYPE_ECHO_RESPONSE &&
                                  response.header.request_id == 2U && response.payload_len == payload_len &&
                                  memcmp(response.payload, payload, payload_len) == 0;

    dispatcher_fixture_destroy(&fixture);

    TEST_ASSERT(result == TRANSPORT_IO_OK);

    TEST_ASSERT(decoded);
    TEST_ASSERT(response_matches);

    return true;
}

static bool test_dispatcher_echo_uses_decoded_payload_length(void) {
    DispatcherFixture fixture;

    TEST_ASSERT(dispatcher_fixture_init(&fixture));

    uint8_t payload[2048];

    memset(payload, 'Q', sizeof(payload));

    const uint32_t payload_len = (uint32_t)sizeof(payload);

    DecodedFrame request = make_request(MSG_TYPE_ECHO_REQUEST, 3U, payload, payload_len);

    // Заголовок описывает wire payload,
    // а request.payload уже содержит распакованные данные.
    request.header.flags = PACKET_FLAG_COMPRESSED;

    request.header.payload_len      = 32U;
    request.header.uncompressed_len = payload_len;

    const TransportIoResult result = server_dispatch_frame(&fixture.dispatch, &request);

    ParsedFrame  parsed   = {0};
    DecodedFrame response = {0};

    const bool decoded = decode_queued_frame(&fixture, &parsed, &response);

    const bool response_matches = decoded && response.header.type == MSG_TYPE_ECHO_RESPONSE &&
                                  response.header.request_id == 3U && response.payload_len == payload_len &&
                                  memcmp(response.payload, payload, payload_len) == 0;

    dispatcher_fixture_destroy(&fixture);

    TEST_ASSERT(result == TRANSPORT_IO_OK);

    TEST_ASSERT(decoded);
    TEST_ASSERT(response_matches);

    return true;
}

static bool test_dispatcher_unsupported_request(void) {
    DispatcherFixture fixture;

    TEST_ASSERT(dispatcher_fixture_init(&fixture));

    static const uint8_t request_payload[] = "registration";

    static const uint8_t expected_message[] = "Message type is not implemented";

    const uint32_t request_payload_len = (uint32_t)(sizeof(request_payload) - 1U);

    const uint16_t expected_message_len = (uint16_t)(sizeof(expected_message) - 1U);

    const DecodedFrame request = make_request(MSG_TYPE_REGISTER_REQUEST, 4U, request_payload, request_payload_len);

    const TransportIoResult result = server_dispatch_frame(&fixture.dispatch, &request);

    ParsedFrame   parsed   = {0};
    DecodedFrame  response = {0};
    ErrorResponse error    = {0};

    const bool decoded = decode_queued_frame(&fixture, &parsed, &response);

    const bool error_decoded = decoded && error_response_decode(response.payload, response.payload_len, &error);

    const bool response_matches = error_decoded && response.header.type == MSG_TYPE_ERROR_RESPONSE &&
                                  response.header.request_id == 4U && error.code == ERROR_CODE_NOT_IMPLEMENTED &&
                                  error.message_len == expected_message_len &&
                                  memcmp(error.message, expected_message, expected_message_len) == 0;

    dispatcher_fixture_destroy(&fixture);

    TEST_ASSERT(result == TRANSPORT_IO_OK);

    TEST_ASSERT(decoded);
    TEST_ASSERT(error_decoded);
    TEST_ASSERT(response_matches);

    return true;
}

static bool test_dispatcher_rejects_server_message(void) {
    DispatcherFixture fixture;

    TEST_ASSERT(dispatcher_fixture_init(&fixture));

    const DecodedFrame response = make_request(MSG_TYPE_PING_RESPONSE, 5U, NULL, 0U);

    const TransportIoResult result = server_dispatch_frame(&fixture.dispatch, &response);

    const size_t queued_bytes = fixture.connection.transport.write_bytes;

    dispatcher_fixture_destroy(&fixture);

    TEST_ASSERT(result == TRANSPORT_IO_PROTOCOL_ERROR);

    TEST_ASSERT(queued_bytes == 0U);

    return true;
}

static bool test_dispatcher_propagates_queue_error(void) {
    DispatcherFixture fixture;

    TEST_ASSERT(dispatcher_fixture_init(&fixture));

    fixture.connection.transport.write_offset = 0U;

    fixture.connection.transport.write_bytes = sizeof(fixture.connection.transport.write_buffer);

    const DecodedFrame request = make_request(MSG_TYPE_PING_REQUEST, 6U, NULL, 0U);

    const TransportIoResult result = server_dispatch_frame(&fixture.dispatch, &request);

    dispatcher_fixture_destroy(&fixture);

    TEST_ASSERT(result == TRANSPORT_IO_BUFFER_FULL);

    return true;
}

void register_dispatcher_tests(TestSuite* suite) {
    TEST_ADD(suite, test_dispatcher_ping);

    TEST_ADD(suite, test_dispatcher_echo);

    TEST_ADD(suite, test_dispatcher_echo_uses_decoded_payload_length);

    TEST_ADD(suite, test_dispatcher_unsupported_request);

    TEST_ADD(suite, test_dispatcher_rejects_server_message);

    TEST_ADD(suite, test_dispatcher_propagates_queue_error);
}

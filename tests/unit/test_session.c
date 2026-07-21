#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "server/session.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define CAPTURE_MAX_FRAMES 8U
#define CAPTURE_MAX_PAYLOAD 256U

typedef struct {
    size_t          count;
    MessageType     types[CAPTURE_MAX_FRAMES];
    uint32_t        request_ids[CAPTURE_MAX_FRAMES];
    uint32_t        payload_lengths[CAPTURE_MAX_FRAMES];
    uint8_t         payloads[CAPTURE_MAX_FRAMES][CAPTURE_MAX_PAYLOAD];
    SessionIoResult result;
} CaptureContext;

static int set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);

    if(flags < 0) {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int create_socket_pair(int sockets[2]) {
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        return -1;
    }

    if(set_nonblocking(sockets[0]) != 0) {
        (void)close(sockets[0]);
        (void)close(sockets[1]);
        return -1;
    }

    const struct timeval timeout = {
        .tv_sec = 1,
        .tv_usec = 0,
    };

    if(setsockopt(sockets[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        (void)close(sockets[0]);
        (void)close(sockets[1]);
        return -1;
    }

    return 0;
}

static int write_all(int fd, const uint8_t* data, size_t size) {
    size_t offset = 0U;

    while(offset < size) {
        const ssize_t written = write(fd, data + offset, size - offset);

        if(written > 0) {
            offset += (size_t)written;
            continue;
        }

        if(written < 0 && errno == EINTR) {
            continue;
        }

        return -1;
    }

    return 0;
}

static int read_exact(int fd, uint8_t* data, size_t size) {
    size_t offset = 0U;

    while(offset < size) {
        const ssize_t received = read(fd, data + offset, size - offset);

        if(received > 0) {
            offset += (size_t)received;
            continue;
        }

        if(received < 0 && errno == EINTR) {
            continue;
        }

        return -1;
    }

    return 0;
}

static SessionIoResult capture_handler(void* context, ClientSession* session, const ParsedFrame* frame) {
    (void)session;

    CaptureContext* capture = context;

    if(capture->count >= CAPTURE_MAX_FRAMES || frame->header.payload_len > CAPTURE_MAX_PAYLOAD) {
        return SESSION_IO_BUFFER_FULL;
    }

    const size_t index = capture->count;

    capture->types[index]           = frame->header.type;
    capture->request_ids[index]     = frame->header.request_id;
    capture->payload_lengths[index] = frame->header.payload_len;

    if(frame->header.payload_len != 0U) {
        memcpy(capture->payloads[index], frame->payload, frame->header.payload_len);
    }

    ++capture->count;
    return capture->result;
}

static size_t build_frame(
    uint8_t* output,
    size_t capacity,
    MessageType type,
    uint32_t request_id,
    const uint8_t* payload,
    uint32_t payload_len
) {
    size_t output_size = 0U;

    if(frame_build_uncompressed(type, request_id, payload, payload_len, output, capacity, &output_size) != 0) {
        return 0U;
    }

    return output_size;
}

static void test_session_reset_and_init(void) {
    ClientSession session;
    memset(&session, 0xA5, sizeof(session));

    session_reset(&session);

    TEST_ASSERT(!session_is_active(&session));
    TEST_ASSERT_EQ_INT(-1, session.socket_fd);
    TEST_ASSERT_EQ_U32(0U, session.user_id);
    TEST_ASSERT_EQ_SIZE(0U, session.read_bytes);
    TEST_ASSERT_EQ_SIZE(0U, session.write_offset);
    TEST_ASSERT_EQ_SIZE(0U, session.write_bytes);
    TEST_ASSERT(!session_has_pending_write(&session));

    session_init(&session, 42);

    TEST_ASSERT(session_is_active(&session));
    TEST_ASSERT_EQ_INT(42, session.socket_fd);
    TEST_ASSERT_EQ_U32(0U, session.user_id);
    TEST_ASSERT_EQ_SIZE(0U, session.read_bytes);
    TEST_ASSERT_EQ_SIZE(0U, session.write_bytes);
}

static void test_session_queue_frame(void) {
    static const uint8_t payload[] = "queued payload";
    ClientSession session;
    session_init(&session, 10);

    TEST_ASSERT_EQ_INT(
        SESSION_IO_OK,
        session_queue_frame(
            &session, MSG_TYPE_ECHO_RESPONSE, 7U, payload, (uint32_t)(sizeof(payload) - 1U)
        )
    );
    TEST_ASSERT(session_has_pending_write(&session));

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(
        FRAME_PARSE_COMPLETE, frame_try_parse(session.write_buffer, session.write_bytes, &frame)
    );
    TEST_ASSERT_EQ_INT(MSG_TYPE_ECHO_RESPONSE, frame.header.type);
    TEST_ASSERT_EQ_U32(7U, frame.header.request_id);
    TEST_ASSERT_MEMORY(payload, frame.payload, sizeof(payload) - 1U);
}

static void test_session_queue_frame_errors(void) {
    static const uint8_t byte = 1U;
    ClientSession session;
    session_init(&session, 10);

    TEST_ASSERT_EQ_INT(
        SESSION_IO_INVALID_ARGUMENT,
        session_queue_frame(&session, MSG_TYPE_ECHO_RESPONSE, 1U, NULL, 1U)
    );
    TEST_ASSERT_EQ_INT(
        SESSION_IO_INVALID_ARGUMENT,
        session_queue_frame(
            &session, MSG_TYPE_ECHO_RESPONSE, 1U, &byte, PROTOCOL_MAX_PAYLOAD_LENGTH + 1U
        )
    );
    TEST_ASSERT_EQ_INT(
        SESSION_IO_FRAME_BUILD_ERROR,
        session_queue_frame(&session, MSG_TYPE_PING_RESPONSE, 0U, NULL, 0U)
    );
}

static void test_session_write_buffer_full(void) {
    ClientSession session;
    session_init(&session, 10);

    session.write_bytes = sizeof(session.write_buffer) - 1U;

    TEST_ASSERT_EQ_INT(
        SESSION_IO_BUFFER_FULL,
        session_queue_frame(&session, MSG_TYPE_PING_RESPONSE, 1U, NULL, 0U)
    );
}

static void test_session_write_buffer_compaction(void) {
    static const uint8_t payload[] = "middle";
    ClientSession session;
    session_init(&session, 10);

    TEST_ASSERT_EQ_INT(
        SESSION_IO_OK, session_queue_frame(&session, MSG_TYPE_PING_RESPONSE, 1U, NULL, 0U)
    );
    const size_t first_size = session.write_bytes;

    TEST_ASSERT_EQ_INT(
        SESSION_IO_OK,
        session_queue_frame(
            &session, MSG_TYPE_ECHO_RESPONSE, 2U, payload, (uint32_t)(sizeof(payload) - 1U)
        )
    );
    const size_t second_size = session.write_bytes - first_size;

    session.write_offset = first_size;

    TEST_ASSERT_EQ_INT(
        SESSION_IO_OK, session_queue_frame(&session, MSG_TYPE_PING_RESPONSE, 3U, NULL, 0U)
    );

    ParsedFrame second;
    ParsedFrame third;

    TEST_ASSERT_EQ_INT(
        FRAME_PARSE_COMPLETE, frame_try_parse(session.write_buffer, session.write_bytes, &second)
    );
    TEST_ASSERT_EQ_INT(MSG_TYPE_ECHO_RESPONSE, second.header.type);
    TEST_ASSERT_EQ_U32(2U, second.header.request_id);
    TEST_ASSERT_EQ_SIZE(second_size, second.frame_size);

    TEST_ASSERT_EQ_INT(
        FRAME_PARSE_COMPLETE,
        frame_try_parse(
            session.write_buffer + second.frame_size, session.write_bytes - second.frame_size, &third
        )
    );
    TEST_ASSERT_EQ_INT(MSG_TYPE_PING_RESPONSE, third.header.type);
    TEST_ASSERT_EQ_U32(3U, third.header.request_id);
    TEST_ASSERT_EQ_SIZE(0U, session.write_offset);
}

static void test_session_handle_write(void) {
    int sockets[2];
    TEST_ASSERT_EQ_INT(0, create_socket_pair(sockets));

    static const uint8_t payload[] = "socket write";
    ClientSession session;
    session_init(&session, sockets[0]);

    TEST_ASSERT_EQ_INT(
        SESSION_IO_OK,
        session_queue_frame(
            &session, MSG_TYPE_ECHO_RESPONSE, 20U, payload, (uint32_t)(sizeof(payload) - 1U)
        )
    );

    const size_t expected_size = session.write_bytes;
    uint8_t received[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];

    TEST_ASSERT_EQ_INT(SESSION_IO_OK, session_handle_write(&session));
    TEST_ASSERT(!session_has_pending_write(&session));
    TEST_ASSERT_EQ_SIZE(0U, session.write_bytes);
    TEST_ASSERT_EQ_INT(0, read_exact(sockets[1], received, expected_size));

    ParsedFrame frame;
    TEST_ASSERT_EQ_INT(FRAME_PARSE_COMPLETE, frame_try_parse(received, expected_size, &frame));
    TEST_ASSERT_EQ_U32(20U, frame.header.request_id);
    TEST_ASSERT_MEMORY(payload, frame.payload, sizeof(payload) - 1U);

    (void)close(sockets[0]);
    (void)close(sockets[1]);
}

static void test_session_handle_read_single_frame(void) {
    int sockets[2];
    TEST_ASSERT_EQ_INT(0, create_socket_pair(sockets));

    static const uint8_t payload[] = "socket read";
    uint8_t raw_frame[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    const size_t raw_size = build_frame(
        raw_frame, sizeof(raw_frame), MSG_TYPE_ECHO_REQUEST, 30U, payload, (uint32_t)(sizeof(payload) - 1U)
    );

    TEST_ASSERT(raw_size != 0U);
    TEST_ASSERT_EQ_INT(0, write_all(sockets[1], raw_frame, raw_size));

    ClientSession session;
    session_init(&session, sockets[0]);

    CaptureContext capture = {.result = SESSION_IO_OK};

    TEST_ASSERT_EQ_INT(SESSION_IO_OK, session_handle_read(&session, capture_handler, &capture));
    TEST_ASSERT_EQ_SIZE(1U, capture.count);
    TEST_ASSERT_EQ_INT(MSG_TYPE_ECHO_REQUEST, capture.types[0]);
    TEST_ASSERT_EQ_U32(30U, capture.request_ids[0]);
    TEST_ASSERT_EQ_U32(sizeof(payload) - 1U, capture.payload_lengths[0]);
    TEST_ASSERT_MEMORY(payload, capture.payloads[0], sizeof(payload) - 1U);
    TEST_ASSERT_EQ_SIZE(0U, session.read_bytes);

    (void)close(sockets[0]);
    (void)close(sockets[1]);
}

static void test_session_handle_read_multiple_frames(void) {
    int sockets[2];
    TEST_ASSERT_EQ_INT(0, create_socket_pair(sockets));

    static const uint8_t payload[] = "second";
    uint8_t raw[2U * PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];

    const size_t first_size = build_frame(raw, sizeof(raw), MSG_TYPE_PING_REQUEST, 40U, NULL, 0U);
    const size_t second_size = build_frame(
        raw + first_size, sizeof(raw) - first_size, MSG_TYPE_ECHO_REQUEST, 41U, payload,
        (uint32_t)(sizeof(payload) - 1U)
    );

    TEST_ASSERT(first_size != 0U);
    TEST_ASSERT(second_size != 0U);
    TEST_ASSERT_EQ_INT(0, write_all(sockets[1], raw, first_size + second_size));

    ClientSession session;
    session_init(&session, sockets[0]);

    CaptureContext capture = {.result = SESSION_IO_OK};

    TEST_ASSERT_EQ_INT(SESSION_IO_OK, session_handle_read(&session, capture_handler, &capture));
    TEST_ASSERT_EQ_SIZE(2U, capture.count);
    TEST_ASSERT_EQ_INT(MSG_TYPE_PING_REQUEST, capture.types[0]);
    TEST_ASSERT_EQ_INT(MSG_TYPE_ECHO_REQUEST, capture.types[1]);
    TEST_ASSERT_EQ_U32(41U, capture.request_ids[1]);
    TEST_ASSERT_MEMORY(payload, capture.payloads[1], sizeof(payload) - 1U);

    (void)close(sockets[0]);
    (void)close(sockets[1]);
}

static void test_session_handle_read_partial_frame(void) {
    int sockets[2];
    TEST_ASSERT_EQ_INT(0, create_socket_pair(sockets));

    static const uint8_t payload[] = "fragmented";
    uint8_t raw_frame[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    const size_t raw_size = build_frame(
        raw_frame, sizeof(raw_frame), MSG_TYPE_ECHO_REQUEST, 50U, payload, (uint32_t)(sizeof(payload) - 1U)
    );

    TEST_ASSERT(raw_size > 7U);
    TEST_ASSERT_EQ_INT(0, write_all(sockets[1], raw_frame, 7U));

    ClientSession session;
    session_init(&session, sockets[0]);

    CaptureContext capture = {.result = SESSION_IO_OK};

    TEST_ASSERT_EQ_INT(SESSION_IO_OK, session_handle_read(&session, capture_handler, &capture));
    TEST_ASSERT_EQ_SIZE(0U, capture.count);
    TEST_ASSERT_EQ_SIZE(7U, session.read_bytes);

    TEST_ASSERT_EQ_INT(0, write_all(sockets[1], raw_frame + 7U, raw_size - 7U));
    TEST_ASSERT_EQ_INT(SESSION_IO_OK, session_handle_read(&session, capture_handler, &capture));
    TEST_ASSERT_EQ_SIZE(1U, capture.count);
    TEST_ASSERT_EQ_SIZE(0U, session.read_bytes);

    (void)close(sockets[0]);
    (void)close(sockets[1]);
}

static void test_session_handle_read_protocol_error(void) {
    int sockets[2];
    TEST_ASSERT_EQ_INT(0, create_socket_pair(sockets));

    static const uint8_t payload[] = "bad crc";
    uint8_t raw_frame[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    const size_t raw_size = build_frame(
        raw_frame, sizeof(raw_frame), MSG_TYPE_ECHO_REQUEST, 60U, payload, (uint32_t)(sizeof(payload) - 1U)
    );

    TEST_ASSERT(raw_size != 0U);
    raw_frame[raw_size - 1U] ^= 0x01U;
    TEST_ASSERT_EQ_INT(0, write_all(sockets[1], raw_frame, raw_size));

    ClientSession session;
    session_init(&session, sockets[0]);
    CaptureContext capture = {.result = SESSION_IO_OK};

    TEST_ASSERT_EQ_INT(
        SESSION_IO_PROTOCOL_ERROR, session_handle_read(&session, capture_handler, &capture)
    );
    TEST_ASSERT_EQ_SIZE(0U, capture.count);

    (void)close(sockets[0]);
    (void)close(sockets[1]);
}

static void test_session_handler_error_is_propagated(void) {
    int sockets[2];
    TEST_ASSERT_EQ_INT(0, create_socket_pair(sockets));

    uint8_t raw_frame[PACKET_HEADER_WIRE_SIZE];
    const size_t raw_size = build_frame(
        raw_frame, sizeof(raw_frame), MSG_TYPE_PING_REQUEST, 70U, NULL, 0U
    );

    TEST_ASSERT_EQ_INT(0, write_all(sockets[1], raw_frame, raw_size));

    ClientSession session;
    session_init(&session, sockets[0]);

    CaptureContext capture = {.result = SESSION_IO_BUFFER_FULL};

    TEST_ASSERT_EQ_INT(
        SESSION_IO_BUFFER_FULL, session_handle_read(&session, capture_handler, &capture)
    );
    TEST_ASSERT_EQ_SIZE(1U, capture.count);
    TEST_ASSERT_EQ_SIZE(raw_size, session.read_bytes);

    (void)close(sockets[0]);
    (void)close(sockets[1]);
}

static void test_session_peer_closed(void) {
    int sockets[2];
    TEST_ASSERT_EQ_INT(0, create_socket_pair(sockets));

    ClientSession session;
    session_init(&session, sockets[0]);
    CaptureContext capture = {.result = SESSION_IO_OK};

    (void)close(sockets[1]);

    TEST_ASSERT_EQ_INT(
        SESSION_IO_PEER_CLOSED, session_handle_read(&session, capture_handler, &capture)
    );

    (void)close(sockets[0]);
}

void register_session_tests(TestSuite* suite) {
    TEST_ADD(suite, test_session_reset_and_init);
    TEST_ADD(suite, test_session_queue_frame);
    TEST_ADD(suite, test_session_queue_frame_errors);
    TEST_ADD(suite, test_session_write_buffer_full);
    TEST_ADD(suite, test_session_write_buffer_compaction);
    TEST_ADD(suite, test_session_handle_write);
    TEST_ADD(suite, test_session_handle_read_single_frame);
    TEST_ADD(suite, test_session_handle_read_multiple_frames);
    TEST_ADD(suite, test_session_handle_read_partial_frame);
    TEST_ADD(suite, test_session_handle_read_protocol_error);
    TEST_ADD(suite, test_session_handler_error_is_propagated);
    TEST_ADD(suite, test_session_peer_closed);
}

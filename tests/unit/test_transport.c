#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "server/transport.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define CAPTURE_MAX_FRAMES  8U
#define CAPTURE_MAX_PAYLOAD 4096U

typedef struct {
    size_t            count;
    MessageType       types[CAPTURE_MAX_FRAMES];
    uint32_t          request_ids[CAPTURE_MAX_FRAMES];
    uint32_t          payload_lengths[CAPTURE_MAX_FRAMES];
    uint8_t           payloads[CAPTURE_MAX_FRAMES][CAPTURE_MAX_PAYLOAD];
    TransportIoResult result;
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

    if(set_nonblocking(sockets[0]) != 0 || set_nonblocking(sockets[1]) != 0) {
        (void)close(sockets[0]);
        (void)close(sockets[1]);
        return -1;
    }

    const struct timeval timeout = {
        .tv_sec  = 1,
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

        if(written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
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

        if(received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }

        return -1;
    }

    return 0;
}

static TransportIoResult capture_handler(void* context, const DecodedFrame* frame) {
    CaptureContext* capture = context;

    if(capture == NULL || frame == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    if(capture->count >= CAPTURE_MAX_FRAMES || frame->payload_len > CAPTURE_MAX_PAYLOAD) {
        return TRANSPORT_IO_BUFFER_FULL;
    }

    const size_t index              = capture->count;
    capture->types[index]           = frame->header.type;
    capture->request_ids[index]     = frame->header.request_id;
    capture->payload_lengths[index] = frame->payload_len;

    if(frame->payload_len != 0U) {
        memcpy(capture->payloads[index], frame->payload, frame->payload_len);
    }

    ++capture->count;
    return capture->result;
}

static size_t build_frame(
    FrameCodec*    codec,
    uint8_t*       output,
    size_t         capacity,
    MessageType    type,
    uint32_t       request_id,
    const uint8_t* payload,
    uint32_t       payload_len
) {
    size_t output_size = 0U;

    if(frame_build(codec, type, request_id, payload, payload_len, output, capacity, &output_size) != 0) {
        return 0U;
    }

    return output_size;
}

static bool test_transport_reset_and_init(void) {
    TransportSession transport;
    memset(&transport, 0xA5, sizeof(transport));

    transport_reset(&transport);

    TEST_ASSERT(!transport_is_active(&transport));
    TEST_ASSERT(transport.socket_fd == -1);
    TEST_ASSERT(transport.read_bytes == 0U);
    TEST_ASSERT(transport.write_offset == 0U);
    TEST_ASSERT(transport.write_bytes == 0U);
    TEST_ASSERT(!transport_has_pending_write(&transport));

    transport_init(&transport, 42);

    TEST_ASSERT(transport_is_active(&transport));
    TEST_ASSERT(transport.socket_fd == 42);
    TEST_ASSERT(transport.read_bytes == 0U);
    TEST_ASSERT(transport.write_offset == 0U);
    TEST_ASSERT(transport.write_bytes == 0U);

    return true;
}

static bool test_transport_queue_and_write_frame(void) {
    int sockets[2];
    TEST_ASSERT(create_socket_pair(sockets) == 0);

    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, sockets[0]);

    static const uint8_t payload[] = "queued response";

    TEST_ASSERT(
        transport_queue_frame(
            &transport, &codec, MSG_TYPE_ECHO_RESPONSE, 10U, payload, (uint32_t)(sizeof(payload) - 1U)
        ) == TRANSPORT_IO_OK
    );
    TEST_ASSERT(transport_has_pending_write(&transport));

    const size_t queued_size = transport.write_bytes;
    TEST_ASSERT(queued_size > PACKET_HEADER_WIRE_SIZE);
    TEST_ASSERT(transport_handle_write(&transport) == TRANSPORT_IO_OK);
    TEST_ASSERT(!transport_has_pending_write(&transport));
    TEST_ASSERT(transport.write_bytes == 0U);
    TEST_ASSERT(transport.write_offset == 0U);

    uint8_t wire[PACKET_HEADER_WIRE_SIZE + sizeof(payload)] = {0};
    TEST_ASSERT(read_exact(sockets[1], wire, queued_size) == 0);

    ParsedFrame  parsed;
    DecodedFrame decoded;
    TEST_ASSERT(frame_try_parse(wire, queued_size, &parsed) == FRAME_PARSE_COMPLETE);
    TEST_ASSERT(frame_decode(&codec, &parsed, &decoded) == FRAME_DECODE_OK);
    TEST_ASSERT(decoded.header.type == MSG_TYPE_ECHO_RESPONSE);
    TEST_ASSERT(decoded.header.request_id == 10U);
    TEST_ASSERT(decoded.payload_len == sizeof(payload) - 1U);
    TEST_ASSERT(memcmp(decoded.payload, payload, sizeof(payload) - 1U) == 0);

    (void)close(sockets[0]);
    (void)close(sockets[1]);

    return true;
}

static bool test_transport_fragmented_read(void) {
    int sockets[2];
    TEST_ASSERT(create_socket_pair(sockets) == 0);

    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, sockets[0]);

    static const uint8_t payload[] = "fragmented frame";
    uint8_t              wire[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    const size_t         wire_size =
        build_frame(&codec, wire, sizeof(wire), MSG_TYPE_ECHO_REQUEST, 20U, payload, (uint32_t)(sizeof(payload) - 1U));

    TEST_ASSERT(wire_size != 0U);

    CaptureContext capture    = {.result = TRANSPORT_IO_OK};
    const size_t   first_part = 7U;

    TEST_ASSERT(write_all(sockets[1], wire, first_part) == 0);
    TEST_ASSERT(transport_handle_read(&transport, &codec, capture_handler, &capture) == TRANSPORT_IO_OK);
    TEST_ASSERT(capture.count == 0U);
    TEST_ASSERT(transport.read_bytes == first_part);

    TEST_ASSERT(write_all(sockets[1], wire + first_part, wire_size - first_part) == 0);
    TEST_ASSERT(transport_handle_read(&transport, &codec, capture_handler, &capture) == TRANSPORT_IO_OK);
    TEST_ASSERT(capture.count == 1U);
    TEST_ASSERT(capture.types[0] == MSG_TYPE_ECHO_REQUEST);
    TEST_ASSERT(capture.request_ids[0] == 20U);
    TEST_ASSERT(capture.payload_lengths[0] == sizeof(payload) - 1U);
    TEST_ASSERT(memcmp(capture.payloads[0], payload, sizeof(payload) - 1U) == 0);
    TEST_ASSERT(transport.read_bytes == 0U);

    (void)close(sockets[0]);
    (void)close(sockets[1]);

    return true;
}

static bool test_transport_multiple_frames(void) {
    int sockets[2];
    TEST_ASSERT(create_socket_pair(sockets) == 0);

    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, sockets[0]);

    static const uint8_t payload[] = "second frame";
    uint8_t              wire[2U * PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];

    const size_t first_size  = build_frame(&codec, wire, sizeof(wire), MSG_TYPE_PING_REQUEST, 30U, NULL, 0U);
    const size_t second_size = build_frame(
        &codec, wire + first_size, sizeof(wire) - first_size, MSG_TYPE_ECHO_REQUEST, 31U, payload,
        (uint32_t)(sizeof(payload) - 1U)
    );

    TEST_ASSERT(first_size != 0U);
    TEST_ASSERT(second_size != 0U);
    TEST_ASSERT(write_all(sockets[1], wire, first_size + second_size) == 0);

    CaptureContext capture = {.result = TRANSPORT_IO_OK};
    TEST_ASSERT(transport_handle_read(&transport, &codec, capture_handler, &capture) == TRANSPORT_IO_OK);
    TEST_ASSERT(capture.count == 2U);
    TEST_ASSERT(capture.types[0] == MSG_TYPE_PING_REQUEST);
    TEST_ASSERT(capture.request_ids[0] == 30U);
    TEST_ASSERT(capture.payload_lengths[0] == 0U);
    TEST_ASSERT(capture.types[1] == MSG_TYPE_ECHO_REQUEST);
    TEST_ASSERT(capture.request_ids[1] == 31U);
    TEST_ASSERT(memcmp(capture.payloads[1], payload, sizeof(payload) - 1U) == 0);

    (void)close(sockets[0]);
    (void)close(sockets[1]);

    return true;
}

static bool test_transport_decodes_compressed_frame(void) {
    int sockets[2];
    TEST_ASSERT(create_socket_pair(sockets) == 0);

    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, sockets[0]);

    uint8_t payload[2048];
    memset(payload, 'Z', sizeof(payload));

    uint8_t      wire[PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH];
    const size_t wire_size =
        build_frame(&codec, wire, sizeof(wire), MSG_TYPE_ECHO_REQUEST, 40U, payload, sizeof(payload));

    TEST_ASSERT(wire_size != 0U);

    ParsedFrame parsed;
    TEST_ASSERT(frame_try_parse(wire, wire_size, &parsed) == FRAME_PARSE_COMPLETE);
    TEST_ASSERT((parsed.header.flags & PACKET_FLAG_COMPRESSED) != 0U);

    TEST_ASSERT(write_all(sockets[1], wire, wire_size) == 0);

    CaptureContext capture = {.result = TRANSPORT_IO_OK};
    TEST_ASSERT(transport_handle_read(&transport, &codec, capture_handler, &capture) == TRANSPORT_IO_OK);
    TEST_ASSERT(capture.count == 1U);
    TEST_ASSERT(capture.payload_lengths[0] == sizeof(payload));
    TEST_ASSERT(memcmp(capture.payloads[0], payload, sizeof(payload)) == 0);

    (void)close(sockets[0]);
    (void)close(sockets[1]);

    return true;
}

static bool test_transport_propagates_handler_result(void) {
    int sockets[2];
    TEST_ASSERT(create_socket_pair(sockets) == 0);

    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, sockets[0]);

    uint8_t      wire[PACKET_HEADER_WIRE_SIZE];
    const size_t wire_size = build_frame(&codec, wire, sizeof(wire), MSG_TYPE_PING_REQUEST, 50U, NULL, 0U);

    TEST_ASSERT(write_all(sockets[1], wire, wire_size) == 0);

    CaptureContext capture = {.result = TRANSPORT_IO_SYSTEM_ERROR};
    TEST_ASSERT(transport_handle_read(&transport, &codec, capture_handler, &capture) == TRANSPORT_IO_SYSTEM_ERROR);
    TEST_ASSERT(capture.count == 1U);
    TEST_ASSERT(transport.read_bytes == wire_size);

    (void)close(sockets[0]);
    (void)close(sockets[1]);

    return true;
}

static bool test_transport_rejects_invalid_crc(void) {
    int sockets[2];
    TEST_ASSERT(create_socket_pair(sockets) == 0);

    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, sockets[0]);

    static const uint8_t payload[] = "bad crc";
    uint8_t              wire[PACKET_HEADER_WIRE_SIZE + sizeof(payload) - 1U];
    const size_t         wire_size =
        build_frame(&codec, wire, sizeof(wire), MSG_TYPE_ECHO_REQUEST, 60U, payload, (uint32_t)(sizeof(payload) - 1U));

    wire[wire_size - 1U] ^= 0x01U;
    TEST_ASSERT(write_all(sockets[1], wire, wire_size) == 0);

    CaptureContext capture = {.result = TRANSPORT_IO_OK};
    TEST_ASSERT(transport_handle_read(&transport, &codec, capture_handler, &capture) == TRANSPORT_IO_PROTOCOL_ERROR);
    TEST_ASSERT(capture.count == 0U);

    (void)close(sockets[0]);
    (void)close(sockets[1]);

    return true;
}

static bool test_transport_peer_closed(void) {
    int sockets[2];
    TEST_ASSERT(create_socket_pair(sockets) == 0);

    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, sockets[0]);

    (void)close(sockets[1]);

    CaptureContext capture = {.result = TRANSPORT_IO_OK};
    TEST_ASSERT(transport_handle_read(&transport, &codec, capture_handler, &capture) == TRANSPORT_IO_PEER_CLOSED);

    (void)close(sockets[0]);

    return true;
}

static bool test_transport_queue_errors(void) {
    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, 10);

    static const uint8_t payload[] = {1U};

    TEST_ASSERT(
        transport_queue_frame(&transport, NULL, MSG_TYPE_ECHO_RESPONSE, 1U, payload, sizeof(payload)) ==
        TRANSPORT_IO_INVALID_ARGUMENT
    );

    TEST_ASSERT(
        transport_queue_frame(&transport, &codec, MSG_TYPE_ECHO_RESPONSE, 1U, NULL, 1U) == TRANSPORT_IO_INVALID_ARGUMENT
    );

    TEST_ASSERT(
        transport_queue_frame(
            &transport, &codec, MSG_TYPE_ECHO_RESPONSE, 1U, payload, PROTOCOL_MAX_UNCOMPRESSED_LENGTH + 1U
        ) == TRANSPORT_IO_INVALID_ARGUMENT
    );

    transport.write_offset = 0U;
    transport.write_bytes  = sizeof(transport.write_buffer);

    TEST_ASSERT(
        transport_queue_frame(&transport, &codec, MSG_TYPE_PING_RESPONSE, 2U, NULL, 0U) == TRANSPORT_IO_BUFFER_FULL
    );

    return true;
}

static bool test_transport_compacts_write_buffer(void) {
    TransportSession transport;
    FrameCodec       codec;
    transport_init(&transport, 10);

    transport.write_buffer[0] = 0xAAU;
    transport.write_buffer[1] = 0xBBU;
    transport.write_buffer[2] = 0xCCU;
    transport.write_buffer[3] = 0xDDU;
    transport.write_offset    = 2U;
    transport.write_bytes     = 4U;

    TEST_ASSERT(transport_queue_frame(&transport, &codec, MSG_TYPE_PING_RESPONSE, 3U, NULL, 0U) == TRANSPORT_IO_OK);

    TEST_ASSERT(transport.write_offset == 0U);
    TEST_ASSERT(transport.write_buffer[0] == 0xCCU);
    TEST_ASSERT(transport.write_buffer[1] == 0xDDU);
    TEST_ASSERT(transport.write_bytes == 2U + PACKET_HEADER_WIRE_SIZE);

    return true;
}

void register_transport_tests(TestSuite* suite) {
    TEST_ADD(suite, test_transport_reset_and_init);
    TEST_ADD(suite, test_transport_queue_and_write_frame);
    TEST_ADD(suite, test_transport_fragmented_read);
    TEST_ADD(suite, test_transport_multiple_frames);
    TEST_ADD(suite, test_transport_decodes_compressed_frame);
    TEST_ADD(suite, test_transport_propagates_handler_result);
    TEST_ADD(suite, test_transport_rejects_invalid_crc);
    TEST_ADD(suite, test_transport_peer_closed);
    TEST_ADD(suite, test_transport_queue_errors);
    TEST_ADD(suite, test_transport_compacts_write_buffer);
}

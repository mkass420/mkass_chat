#include "server/transport.h"

#include "common/frame.h"
#include "common/protocol.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sys/socket.h>

static_assert(
    TRANSPORT_READ_BUFFER_SIZE >= PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH,
    "Transport read buffer must fit one maximum frame"
);
static_assert(
    TRANSPORT_WRITE_BUFFER_SIZE >= PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH,
    "Transport write buffer must fit one maximum frame"
);

static void transport_assert(const TransportSession* transport) {
    assert(transport != NULL);
    assert(transport->read_bytes <= sizeof(transport->read_buffer));
    assert(transport->write_offset <= transport->write_bytes);
    assert(transport->write_bytes <= sizeof(transport->write_buffer));
}

void transport_reset(TransportSession* transport) {
    assert(transport != NULL);

    transport->socket_fd    = -1;
    transport->read_bytes   = 0U;
    transport->write_offset = 0U;
    transport->write_bytes  = 0U;
}

void transport_init(TransportSession* transport, int socket_fd) {
    assert(transport != NULL);
    assert(socket_fd >= 0);

    transport_reset(transport);
    transport->socket_fd = socket_fd;
}

bool transport_is_active(const TransportSession* transport) { return transport != NULL && transport->socket_fd >= 0; }

bool transport_has_pending_write(const TransportSession* transport) {
    return transport != NULL && transport->write_offset < transport->write_bytes;
}

static void transport_compact_write_buffer(TransportSession* transport) {
    transport_assert(transport);

    if(transport->write_offset == 0U) {
        return;
    }

    const size_t remaining = transport->write_bytes - transport->write_offset;

    // Сдвигаем неотправленный хвост в начало буфера.
    if(remaining != 0U) {
        memmove(transport->write_buffer, transport->write_buffer + transport->write_offset, remaining);
    }

    transport->write_offset = 0U;
    transport->write_bytes  = remaining;
}

TransportIoResult transport_queue_frame(
    TransportSession* transport,
    MessageType       type,
    uint32_t          request_id,
    const uint8_t*    payload,
    uint32_t          payload_len
) {
    assert(transport_is_active(transport));
    transport_assert(transport);

    if(payload_len != 0U && payload == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }
    if(payload_len > PROTOCOL_MAX_PAYLOAD_LENGTH) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    transport_compact_write_buffer(transport);

    const size_t required_size  = PACKET_HEADER_WIRE_SIZE + (size_t)payload_len;
    const size_t available_size = sizeof(transport->write_buffer) - transport->write_bytes;

    if(required_size > available_size) {
        return TRANSPORT_IO_BUFFER_FULL;
    }

    uint8_t* output     = transport->write_buffer + transport->write_bytes;
    size_t   frame_size = 0U;

    const int build_result =
        frame_build_uncompressed(type, request_id, payload, payload_len, output, available_size, &frame_size);

    if(build_result != 0 || frame_size > available_size) {
        return TRANSPORT_IO_FRAME_BUILD_ERROR;
    }

    transport->write_bytes += frame_size;
    transport_assert(transport);

    return TRANSPORT_IO_OK;
}

static void transport_consume_input(TransportSession* transport, size_t consumed_size) {
    transport_assert(transport);
    assert(consumed_size <= transport->read_bytes);

    const size_t remaining = transport->read_bytes - consumed_size;

    if(remaining != 0U) {
        memmove(transport->read_buffer, transport->read_buffer + consumed_size, remaining);
    }

    transport->read_bytes = remaining;
    transport_assert(transport);
}

static TransportIoResult transport_process_input(
    TransportSession*     transport,
    TransportFrameHandler frame_handler,
    void*                 handler_context
) {
    assert(transport != NULL);
    assert(frame_handler != NULL);
    transport_assert(transport);

    for(;;) {
        ParsedFrame frame;

        const FrameParseResult parse_result = frame_try_parse(transport->read_buffer, transport->read_bytes, &frame);

        if(parse_result == FRAME_PARSE_INCOMPLETE) { // Ждём полный кадр.
            return TRANSPORT_IO_OK;
        }

        if(parse_result != FRAME_PARSE_COMPLETE) {
            return TRANSPORT_IO_PROTOCOL_ERROR;
        }

        if(frame.frame_size == 0U || frame.frame_size > transport->read_bytes) {
            return TRANSPORT_IO_PROTOCOL_ERROR;
        }

        // Payload действует только до сдвига входного буфера.
        const TransportIoResult handler_result = frame_handler(handler_context, &frame);

        if(handler_result != TRANSPORT_IO_OK) {
            return handler_result;
        }

        transport_consume_input(transport, frame.frame_size);
    }
}

TransportIoResult transport_handle_read(
    TransportSession*     transport,
    TransportFrameHandler frame_handler,
    void*                 handler_context
) {
    assert(transport != NULL);
    assert(frame_handler != NULL);
    assert(transport_is_active(transport));
    transport_assert(transport);

    for(;;) {
        // Сначала обрабатываем уже накопленные данные.
        const TransportIoResult process_result = transport_process_input(transport, frame_handler, handler_context);

        if(process_result != TRANSPORT_IO_OK) {
            return process_result;
        }

        const size_t available_size = sizeof(transport->read_buffer) - transport->read_bytes;

        // Полный буфер без готового кадра считается переполнением.
        if(available_size == 0U) {
            return TRANSPORT_IO_BUFFER_FULL;
        }

        const ssize_t received =
            recv(transport->socket_fd, transport->read_buffer + transport->read_bytes, available_size, 0);

        if(received > 0) {
            transport->read_bytes += (size_t)received;
            transport_assert(transport);
            continue;
        }

        if(received == 0) {
            return TRANSPORT_IO_PEER_CLOSED;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return TRANSPORT_IO_OK;
        }

        return TRANSPORT_IO_SYSTEM_ERROR;
    }
}

TransportIoResult transport_handle_write(TransportSession* transport) {
    assert(transport != NULL);
    assert(transport_is_active(transport));
    transport_assert(transport);

    while(transport_has_pending_write(transport)) {
        const uint8_t* data      = transport->write_buffer + transport->write_offset;
        const size_t   remaining = transport->write_bytes - transport->write_offset;
        const ssize_t  sent      = send(transport->socket_fd, data, remaining, MSG_NOSIGNAL);

        if(sent > 0) {
            transport->write_offset += (size_t)sent;
            transport_assert(transport);
            continue;
        }

        if(sent == 0) {
            return TRANSPORT_IO_SYSTEM_ERROR;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return TRANSPORT_IO_OK;
        }

        return TRANSPORT_IO_SYSTEM_ERROR;
    }

    transport->write_offset = 0U;
    transport->write_bytes  = 0U;

    transport_assert(transport);

    return TRANSPORT_IO_OK;
}

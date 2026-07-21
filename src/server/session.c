#include "server/session.h"

#include "common/frame.h"
#include "common/protocol.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sys/socket.h>

static_assert(
    SESSION_READ_BUFFER_SIZE >= PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH,
    "Session read buffer must fit one maximum frame"
);
static_assert(
    SESSION_WRITE_BUFFER_SIZE >= PACKET_HEADER_WIRE_SIZE + PROTOCOL_MAX_PAYLOAD_LENGTH,
    "Session write buffer must fit one maximum frame"
);

static void session_assert(const ClientSession* session) {
    assert(session != NULL);
    assert(session->read_bytes <= sizeof(session->read_buffer));
    assert(session->write_offset <= session->write_bytes);
    assert(session->write_bytes <= sizeof(session->write_buffer));
}

void session_reset(ClientSession* session) {
    assert(session != NULL);

    session->socket_fd    = -1;
    session->user_id      = 0U;
    session->read_bytes   = 0U;
    session->write_offset = 0U;
    session->write_bytes  = 0U;
}

void session_init(ClientSession* session, int socket_fd) {
    assert(session != NULL);
    assert(socket_fd >= 0);

    session_reset(session);
    session->socket_fd = socket_fd;
}

bool session_is_active(const ClientSession* session) { return session != NULL && session->socket_fd >= 0; }

bool session_has_pending_write(const ClientSession* session) {
    return session != NULL && session->write_offset < session->write_bytes;
}

static void session_compact_write_buffer(ClientSession* session) {
    session_assert(session);

    if(session->write_offset == 0U) {
        return;
    }

    const size_t remaining = session->write_bytes - session->write_offset;

    // Сдвигаем неотправленный хвост в начало буфера.
    if(remaining != 0U) {
        memmove(session->write_buffer, session->write_buffer + session->write_offset, remaining);
    }

    session->write_offset = 0U;
    session->write_bytes  = remaining;
}

SessionIoResult session_queue_frame(
    ClientSession* session,
    MessageType    type,
    uint32_t       request_id,
    const uint8_t* payload,
    uint32_t       payload_len
) {
    assert(session_is_active(session));
    session_assert(session);

    if(payload_len != 0U && payload == NULL) {
        return SESSION_IO_INVALID_ARGUMENT;
    }
    if(payload_len > PROTOCOL_MAX_PAYLOAD_LENGTH) {
        return SESSION_IO_INVALID_ARGUMENT;
    }

    session_compact_write_buffer(session);

    const size_t required_size  = PACKET_HEADER_WIRE_SIZE + (size_t)payload_len;
    const size_t available_size = sizeof(session->write_buffer) - session->write_bytes;

    if(required_size > available_size) {
        return SESSION_IO_BUFFER_FULL;
    }

    uint8_t* output = session->write_buffer + session->write_bytes;
    size_t frame_size = 0U;

    const int build_result =
        frame_build_uncompressed(type, request_id, payload, payload_len, output, available_size, &frame_size);

    if(build_result != 0 || frame_size > available_size) {
        return SESSION_IO_FRAME_BUILD_ERROR;
    }

    session->write_bytes += frame_size;
    session_assert(session);

    return SESSION_IO_OK;
}

static void session_consume_input(ClientSession* session, size_t consumed_size) {
    session_assert(session);
    assert(consumed_size <= session->read_bytes);

    const size_t remaining = session->read_bytes - consumed_size;

    if(remaining != 0U) {
        memmove(session->read_buffer, session->read_buffer + consumed_size, remaining);
    }

    session->read_bytes = remaining;
    session_assert(session);
}

static SessionIoResult session_process_input(
    ClientSession*      session,
    SessionFrameHandler frame_handler,
    void*               handler_context
) {
    assert(session != NULL);
    assert(frame_handler != NULL);
    session_assert(session);

    for(;;) {
        ParsedFrame frame;

        const FrameParseResult parse_result = frame_try_parse(session->read_buffer, session->read_bytes, &frame);

        if(parse_result == FRAME_PARSE_INCOMPLETE) { // Ждём полный кадр.
            return SESSION_IO_OK;
        }

        if(parse_result != FRAME_PARSE_COMPLETE) {
            return SESSION_IO_PROTOCOL_ERROR;
        }

        if(frame.frame_size == 0U || frame.frame_size > session->read_bytes) {
            return SESSION_IO_PROTOCOL_ERROR;
        }

        // Payload действует только до сдвига входного буфера.
        const SessionIoResult handler_result = frame_handler(handler_context, session, &frame);

        if(handler_result != SESSION_IO_OK) {
            return handler_result;
        }

        session_consume_input(session, frame.frame_size);
    }
}

SessionIoResult session_handle_read(ClientSession* session, SessionFrameHandler frame_handler, void* handler_context) {
    assert(session != NULL);
    assert(frame_handler != NULL);
    assert(session_is_active(session));
    session_assert(session);

    for(;;) {
        // Сначала обрабатываем уже накопленные данные.
        const SessionIoResult process_result = session_process_input(session, frame_handler, handler_context);

        if(process_result != SESSION_IO_OK) {
            return process_result;
        }

        const size_t available_size = sizeof(session->read_buffer) - session->read_bytes;

        // Полный буфер без готового кадра считается переполнением.
        if(available_size == 0U) {
            return SESSION_IO_BUFFER_FULL;
        }

        const ssize_t received =
            recv(session->socket_fd, session->read_buffer + session->read_bytes, available_size, 0);

        if(received > 0) {
            session->read_bytes += (size_t)received;
            session_assert(session);
            continue;
        }

        if(received == 0) {
            return SESSION_IO_PEER_CLOSED;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return SESSION_IO_OK;
        }

        return SESSION_IO_SYSTEM_ERROR;
    }
}

SessionIoResult session_handle_write(ClientSession* session) {
    assert(session != NULL);
    assert(session_is_active(session));
    session_assert(session);

    while(session_has_pending_write(session)) {
        const uint8_t* data = session->write_buffer + session->write_offset;
        const size_t remaining = session->write_bytes - session->write_offset;
        const ssize_t sent = send(session->socket_fd, data, remaining, MSG_NOSIGNAL);

        if(sent > 0) {
            session->write_offset += (size_t)sent;
            session_assert(session);
            continue;
        }

        if(sent == 0) {
            return SESSION_IO_SYSTEM_ERROR;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return SESSION_IO_OK;
        }

        return SESSION_IO_SYSTEM_ERROR;
    }

    session->write_offset = 0U;
    session->write_bytes  = 0U;

    session_assert(session);

    return SESSION_IO_OK;
}

#include "server/dispatcher.h"

#include "common/protocol.h"
#include "server/state.h"

#include <assert.h>
#include <stdint.h>

static SessionIoResult handle_ping_request(ServerState* server, ClientSession* session, const ParsedFrame* request) {
    (void)server;
    return session_queue_frame(session, MSG_TYPE_PING_RESPONSE, request->header.request_id, NULL, 0U);
}

static SessionIoResult handle_echo_request(ServerState* server, ClientSession* session, const ParsedFrame* request) {
    (void)server;
    return session_queue_frame(
        session, MSG_TYPE_ECHO_RESPONSE, request->header.request_id, request->payload, request->header.payload_len
    );
}

static SessionIoResult handle_unsupported_request(
    ServerState*       server,
    ClientSession*     session,
    const ParsedFrame* request
) {
    (void)server;

    static const uint8_t error_payload[] = "Message type is not implemented";

    return session_queue_frame(
        session, MSG_TYPE_ERROR_RESPONSE, request->header.request_id, error_payload,
        (uint32_t)(sizeof(error_payload) - 1U)
    );
}

SessionIoResult server_dispatch_frame(void* context, ClientSession* session, const ParsedFrame* frame) {
    assert(context != NULL);
    assert(session != NULL);
    assert(frame != NULL);

    ServerState* server = context;

    if(!message_type_is_allowed_from_client(frame->header.type)) {
        return SESSION_IO_PROTOCOL_ERROR;
    }

    switch(frame->header.type) {
        case MSG_TYPE_PING_REQUEST: return handle_ping_request(server, session, frame);
        case MSG_TYPE_ECHO_REQUEST: return handle_echo_request(server, session, frame);
        default: return handle_unsupported_request(server, session, frame);
    }
}

#include "server/dispatcher.h"

#include "common/protocol.h"
#include "server/connection.h"
#include "server/state.h"

#include <assert.h>
#include <stdint.h>

static TransportIoResult handle_ping_request(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
) {
    return transport_queue_frame(
        &connection->transport, &server->frame_codec, MSG_TYPE_PING_RESPONSE, request->header.request_id, NULL, 0U
    );
}

static TransportIoResult handle_echo_request(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
) {
    return transport_queue_frame(
        &connection->transport, &server->frame_codec, MSG_TYPE_ECHO_RESPONSE, request->header.request_id,
        request->payload, request->payload_len
    );
}

static TransportIoResult handle_unsupported_request(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
) {
    static const uint8_t error_payload[] = "Message type is not implemented";

    return transport_queue_frame(
        &connection->transport, &server->frame_codec, MSG_TYPE_ERROR_RESPONSE, request->header.request_id,
        error_payload, (uint32_t)(sizeof(error_payload) - 1U)
    );
}

TransportIoResult server_dispatch_frame(void* context, const DecodedFrame* frame) {
    assert(context != NULL);
    assert(frame != NULL);

    ServerDispatchContext* dispatch = context;

    if(!message_type_is_allowed_from_client(frame->header.type)) {
        return TRANSPORT_IO_PROTOCOL_ERROR;
    }

    switch(frame->header.type) {
        case MSG_TYPE_PING_REQUEST: return handle_ping_request(dispatch->server, dispatch->connection, frame);
        case MSG_TYPE_ECHO_REQUEST: return handle_echo_request(dispatch->server, dispatch->connection, frame);
        default: return handle_unsupported_request(dispatch->server, dispatch->connection, frame);
    }
}

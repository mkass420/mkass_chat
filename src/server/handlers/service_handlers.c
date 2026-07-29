#include "server/handlers/service_handlers.h"

#include "server/transport.h"
#include "server/state.h"

TransportIoResult handle_ping_request(ServerState* server, ClientConnection* connection, const DecodedFrame* request) {
    return transport_queue_frame(
        &connection->transport, &server->frame_codec, MSG_TYPE_PING_RESPONSE, request->header.request_id, NULL, 0U
    );
}

TransportIoResult handle_echo_request(ServerState* server, ClientConnection* connection, const DecodedFrame* request) {
    return transport_queue_frame(
        &connection->transport, &server->frame_codec, MSG_TYPE_ECHO_RESPONSE, request->header.request_id,
        request->payload, request->payload_len
    );
}

TransportIoResult handle_unsupported_request(
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

#include "server/handlers/service_handlers.h"

#include "server/transport.h"
#include "server/state.h"

#include <string.h>

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
    return server_queue_error_response(
        server, connection, request->header.request_id, ERROR_CODE_NOT_IMPLEMENTED, "Message type is not implemented"
    );
}

TransportIoResult server_queue_error_response(
    ServerState*      server,
    ClientConnection* connection,
    uint32_t          request_id,
    ErrorCode         code,
    const char*       message
) {
    if(server == NULL || connection == NULL || request_id == 0U || !error_code_is_valid(code)) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    if(message == NULL) {
        message = error_code_to_string(code);
    }

    const size_t message_len = strlen(message);

    if(message_len > ERROR_MESSAGE_MAX_LENGTH) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    const ErrorResponse response = {
        .code        = code,
        .message     = (const uint8_t*)message,
        .message_len = (uint16_t)message_len,
    };

    uint8_t  payload[ERROR_RESPONSE_MAX_SIZE];
    uint32_t payload_len = 0U;

    if(!error_response_encode(&response, payload, (uint32_t)sizeof(payload), &payload_len)) {
        return TRANSPORT_IO_FRAME_BUILD_ERROR;
    }

    return transport_queue_frame(
        &connection->transport, &server->frame_codec, MSG_TYPE_ERROR_RESPONSE, request_id, payload, payload_len
    );
}

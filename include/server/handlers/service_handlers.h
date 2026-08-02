#ifndef SERVICE_HANDLERS_H
#define SERVICE_HANDLERS_H

#include "server/transport.h"
#include "server/state.h"
#include "server/connection.h"
#include "common/error_protocol.h"

TransportIoResult handle_ping_request(ServerState* server, ClientConnection* connection, const DecodedFrame* request);
TransportIoResult handle_echo_request(ServerState* server, ClientConnection* connection, const DecodedFrame* request);
TransportIoResult handle_unsupported_request(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
);
TransportIoResult server_queue_error_response(
    ServerState*      server,
    ClientConnection* connection,
    uint32_t          request_id,
    ErrorCode         code,
    const char*       message
);

#endif

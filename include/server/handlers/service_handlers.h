#ifndef SERVICE_HANDLERS_H
#define SERVICE_HANDLERS_H

#include "server/transport.h"
#include "server/state.h"

TransportIoResult handle_ping_request(ServerState* server, ClientConnection* connection, const DecodedFrame* request);
TransportIoResult handle_echo_request(ServerState* server, ClientConnection* connection, const DecodedFrame* request);
TransportIoResult handle_unsupported_request(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
);

#endif

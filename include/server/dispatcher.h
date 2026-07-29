#ifndef SERVER_DISPATCHER_H
#define SERVER_DISPATCHER_H

#include "common/frame.h"
#include "server/transport.h"
#include "server/state.h"
#include "server/connection.h"

typedef struct {
    ServerState*      server;
    ClientConnection* connection;
} ServerDispatchContext;

TransportIoResult server_dispatch_frame(void* context, const DecodedFrame* frame);

#endif

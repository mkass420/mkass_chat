#ifndef SERVER_DISPATCHER_H
#define SERVER_DISPATCHER_H

#include "server/session.h"

SessionIoResult server_dispatch_frame(void* context, ClientSession* session, const ParsedFrame* frame);

#endif

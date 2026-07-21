#ifndef SERVER_H
#define SERVER_H

#include "server/state.h"

#include <stdint.h>

int  server_init(ServerState* server, const char* bind_address, uint16_t port);
int  server_run(ServerState* server);
void server_request_stop(void);
void server_destroy(ServerState* server);

#endif

#ifndef FILE_HANDLERS_H
#define FILE_HANDLERS_H

#include "common/frame.h"
#include "server/connection.h"
#include "server/state.h"
#include "server/transport.h"

TransportIoResult server_handle_file_upload_begin(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
);

TransportIoResult server_handle_file_upload_chunk(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
);

TransportIoResult server_handle_file_upload_finish(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
);

TransportIoResult server_handle_file_download_begin(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
);

TransportIoResult server_handle_file_download_chunk(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
);

#endif

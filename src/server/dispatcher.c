#include "server/dispatcher.h"

#include "common/protocol.h"
#include "server/handlers/service_handlers.h"
#include "server/handlers/file_handlers.h"

#include <assert.h>
#include <stdint.h>

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
        case MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST: return server_handle_file_upload_begin(dispatch->server, dispatch->connection, frame);
        case MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST: return server_handle_file_upload_chunk(dispatch->server, dispatch->connection, frame);
        case MSG_TYPE_FILE_UPLOAD_FINISH_REQUEST: return server_handle_file_upload_finish(dispatch->server, dispatch->connection, frame);
        case MSG_TYPE_FILE_DOWNLOAD_BEGIN_REQUEST: return server_handle_file_download_begin(dispatch->server, dispatch->connection, frame);
        case MSG_TYPE_FILE_DOWNLOAD_CHUNK_REQUEST: return server_handle_file_download_chunk(dispatch->server, dispatch->connection, frame);
        default: return handle_unsupported_request(dispatch->server, dispatch->connection, frame);
    }
}

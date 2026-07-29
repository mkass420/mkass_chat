#include "server/handlers/file_handlers.h"

#include "common/file_protocol.h"
#include "common/protocol.h"
#include "config.h"
#include "server/file_repository.h"
#include "server/file_storage.h"
#include "server/file_transfer.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

static TransportIoResult file_queue_frame(
    ServerState*      server,
    ClientConnection* connection,
    MessageType       type,
    uint32_t          request_id,
    const uint8_t*    payload,
    uint32_t          payload_len
) {
    return transport_queue_frame(&connection->transport, &server->frame_codec, type, request_id, payload, payload_len);
}

static TransportIoResult file_queue_error(
    ServerState*      server,
    ClientConnection* connection,
    uint32_t          request_id,
    const char*       message
) {
    const size_t message_len = strlen(message);

    return file_queue_frame(
        server, connection, MSG_TYPE_ERROR_RESPONSE, request_id, (const uint8_t*)message, (uint32_t)message_len
    );
}

static const char* file_transfer_error_message(FileTransferResult result) {
    switch(result) {
        case FILE_TRANSFER_INVALID_ARGUMENT: return "Invalid file request";
        case FILE_TRANSFER_INVALID_STATE: return "Invalid file transfer state";
        case FILE_TRANSFER_BUSY: return "File transfer is already active";
        case FILE_TRANSFER_FILE_TOO_LARGE: return "File is too large";
        case FILE_TRANSFER_INVALID_CHUNK: return "Invalid file chunk";
        case FILE_TRANSFER_INCOMPLETE: return "File upload is incomplete";
        case FILE_TRANSFER_STORAGE_ERROR: return "File storage error";
        case FILE_TRANSFER_OK:
        default: return "File transfer error";
    }
}

static TransportIoResult file_queue_transfer_error(
    ServerState*       server,
    ClientConnection*  connection,
    uint32_t           request_id,
    FileTransferResult result
) {
    return file_queue_error(server, connection, request_id, file_transfer_error_message(result));
}

static TransportIoResult file_queue_repository_error(
    ServerState*         server,
    ClientConnection*    connection,
    uint32_t             request_id,
    FileRepositoryResult result
) {
    const char* message = "File repository error";

    if(result == FILE_REPOSITORY_NOT_FOUND) {
        message = "File not found";
    }
    else if(result == FILE_REPOSITORY_ALREADY_EXISTS) {
        message = "File identifier already exists";
    }

    return file_queue_error(server, connection, request_id, message);
}

static void file_remove_completed_upload(ServerState* server, const FileId* file_id) {
    (void)file_repository_remove(&server->file_repository, file_id);

    (void)file_storage_remove_object(&server->file_storage, file_id);
}

TransportIoResult server_handle_file_upload_begin(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
) {
    if(server == NULL || connection == NULL || request == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    FileUploadBeginRequest upload_request;

    if(!file_upload_begin_request_decode(request->payload, request->payload_len, &upload_request)) {
        return file_queue_error(server, connection, request->header.request_id, "Invalid file upload begin payload");
    }

    const FileTransferResult result = file_upload_begin(
        &server->file_storage, &connection->files.upload, upload_request.file_size, upload_request.file_name,
        upload_request.file_name_len
    );

    if(result != FILE_TRANSFER_OK) {
        return file_queue_transfer_error(server, connection, request->header.request_id, result);
    }

    const TransportIoResult queue_result =
        file_queue_frame(server, connection, MSG_TYPE_FILE_UPLOAD_BEGIN_RESPONSE, request->header.request_id, NULL, 0U);

    if(queue_result != TRANSPORT_IO_OK) {
        file_upload_abort(&server->file_storage, &connection->files.upload);
    }

    return queue_result;
}

TransportIoResult server_handle_file_upload_chunk(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
) {
    if(server == NULL || connection == NULL || request == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    const FileTransferResult result =
        file_upload_write(&connection->files.upload, request->payload, request->payload_len);

    if(result != FILE_TRANSFER_OK) {
        if(result == FILE_TRANSFER_STORAGE_ERROR) {
            file_upload_abort(&server->file_storage, &connection->files.upload);
        }

        return file_queue_transfer_error(server, connection, request->header.request_id, result);
    }

    const TransportIoResult queue_result =
        file_queue_frame(server, connection, MSG_TYPE_FILE_UPLOAD_CHUNK_RESPONSE, request->header.request_id, NULL, 0U);

    if(queue_result != TRANSPORT_IO_OK) {
        file_upload_abort(&server->file_storage, &connection->files.upload);
    }

    return queue_result;
}

TransportIoResult server_handle_file_upload_finish(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
) {
    if(server == NULL || connection == NULL || request == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    CompletedFileUpload completed = {0};

    const FileTransferResult transfer_result =
        file_upload_finish(&server->file_storage, &connection->files.upload, &completed);

    if(transfer_result != FILE_TRANSFER_OK) {
        return file_queue_transfer_error(server, connection, request->header.request_id, transfer_result);
    }

    const time_t now = time(NULL);

    if(now == (time_t)-1) {
        (void)file_storage_remove_object(&server->file_storage, &completed.file_id);

        return file_queue_error(server, connection, request->header.request_id, "Failed to create file metadata");
    }

    FileMetadata metadata = {
        .file_id       = completed.file_id,
        .owner_user_id = connection->user_id,
        .file_size     = completed.file_size,
        .file_crc32    = completed.file_crc32,
        .created_at    = (int64_t)now,
        .file_name_len = completed.file_name_len,
    };

    memcpy(metadata.file_name, completed.file_name, completed.file_name_len);

    metadata.file_name[completed.file_name_len] = '\0';

    const FileRepositoryResult repository_result = file_repository_insert(&server->file_repository, &metadata);

    if(repository_result != FILE_REPOSITORY_OK) {
        (void)file_storage_remove_object(&server->file_storage, &completed.file_id);

        return file_queue_repository_error(server, connection, request->header.request_id, repository_result);
    }

    const FileUploadFinishResponse response = {
        .file_id = completed.file_id,
    };

    uint8_t  payload[FILE_UPLOAD_FINISH_RESPONSE_SIZE];
    uint32_t payload_len = 0U;

    if(!file_upload_finish_response_encode(&response, payload, (uint32_t)sizeof(payload), &payload_len)) {
        file_remove_completed_upload(server, &completed.file_id);

        return TRANSPORT_IO_FRAME_BUILD_ERROR;
    }

    const TransportIoResult queue_result = file_queue_frame(
        server, connection, MSG_TYPE_FILE_UPLOAD_FINISH_RESPONSE, request->header.request_id, payload, payload_len
    );

    if(queue_result != TRANSPORT_IO_OK) {
        file_remove_completed_upload(server, &completed.file_id);
    }

    return queue_result;
}

TransportIoResult server_handle_file_download_begin(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
) {
    if(server == NULL || connection == NULL || request == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    FileDownloadBeginRequest download_request;

    if(!file_download_begin_request_decode(request->payload, request->payload_len, &download_request)) {
        return file_queue_error(server, connection, request->header.request_id, "Invalid file download begin payload");
    }

    FileMetadata metadata;

    const FileRepositoryResult repository_result =
        file_repository_find(&server->file_repository, &download_request.file_id, &metadata);

    if(repository_result != FILE_REPOSITORY_OK) {
        return file_queue_repository_error(server, connection, request->header.request_id, repository_result);
    }

    if(metadata.owner_user_id != connection->user_id) {
        return file_queue_error(server, connection, request->header.request_id, "Access denied");
    }

    const FileTransferResult transfer_result = file_download_begin(
        &server->file_storage, &connection->files.download, &metadata.file_id, (const uint8_t*)metadata.file_name,
        metadata.file_name_len
    );

    if(transfer_result != FILE_TRANSFER_OK) {
        return file_queue_transfer_error(server, connection, request->header.request_id, transfer_result);
    }

    if(connection->files.download.file_size != metadata.file_size) {
        file_download_abort(&connection->files.download);

        return file_queue_error(
            server, connection, request->header.request_id, "Stored file does not match its metadata"
        );
    }

    const FileDownloadBeginResponse response = {
        .file_size     = metadata.file_size,
        .file_name     = (const uint8_t*)metadata.file_name,
        .file_name_len = metadata.file_name_len,
    };

    uint8_t  payload[FILE_DOWNLOAD_BEGIN_RESPONSE_PREFIX_SIZE + FILE_MAX_NAME_LENGTH];
    uint32_t payload_len = 0U;

    if(!file_download_begin_response_encode(&response, payload, (uint32_t)sizeof(payload), &payload_len)) {
        file_download_abort(&connection->files.download);

        return TRANSPORT_IO_FRAME_BUILD_ERROR;
    }

    if(metadata.file_size == 0U) {
        const FileTransferResult finish_result = file_download_finish(&connection->files.download);

        if(finish_result != FILE_TRANSFER_OK) {
            return file_queue_transfer_error(server, connection, request->header.request_id, finish_result);
        }
    }

    const TransportIoResult queue_result = file_queue_frame(
        server, connection, MSG_TYPE_FILE_DOWNLOAD_BEGIN_RESPONSE, request->header.request_id, payload, payload_len
    );

    if(queue_result != TRANSPORT_IO_OK && file_download_is_active(&connection->files.download)) {
        file_download_abort(&connection->files.download);
    }

    return queue_result;
}

TransportIoResult server_handle_file_download_chunk(
    ServerState*        server,
    ClientConnection*   connection,
    const DecodedFrame* request
) {
    if(server == NULL || connection == NULL || request == NULL) {
        return TRANSPORT_IO_INVALID_ARGUMENT;
    }

    uint8_t  payload[FILE_CHUNK_DATA_SIZE];
    uint32_t payload_len = 0U;
    bool     is_complete = false;

    const FileTransferResult read_result =
        file_download_read(&connection->files.download, payload, (uint32_t)sizeof(payload), &payload_len, &is_complete);

    if(read_result != FILE_TRANSFER_OK) {
        if(file_download_is_active(&connection->files.download)) {
            file_download_abort(&connection->files.download);
        }

        return file_queue_transfer_error(server, connection, request->header.request_id, read_result);
    }

    if(payload_len == 0U) {
        if(file_download_is_active(&connection->files.download)) {
            file_download_abort(&connection->files.download);
        }

        return file_queue_error(
            server, connection, request->header.request_id, "File download produced an empty chunk"
        );
    }

    if(is_complete) {
        const FileTransferResult finish_result = file_download_finish(&connection->files.download);

        if(finish_result != FILE_TRANSFER_OK) {
            return file_queue_transfer_error(server, connection, request->header.request_id, finish_result);
        }
    }

    const TransportIoResult queue_result = file_queue_frame(
        server, connection, MSG_TYPE_FILE_DOWNLOAD_CHUNK_RESPONSE, request->header.request_id, payload, payload_len
    );

    if(queue_result != TRANSPORT_IO_OK && file_download_is_active(&connection->files.download)) {
        file_download_abort(&connection->files.download);
    }

    return queue_result;
}

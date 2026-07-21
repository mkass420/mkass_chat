#ifndef SESSION_H
#define SESSION_H

#include "config.h"

#include "common/frame.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    SESSION_IO_OK = 0,
    SESSION_IO_PEER_CLOSED,
    SESSION_IO_PROTOCOL_ERROR,
    SESSION_IO_BUFFER_FULL,
    SESSION_IO_SYSTEM_ERROR,
    SESSION_IO_INVALID_ARGUMENT,
    SESSION_IO_FRAME_BUILD_ERROR
} SessionIoResult;

typedef struct {
    int      socket_fd;                               // Дескриптор сокета (<0 если не активен)
    uint32_t user_id;                                 // ID авторизованного пользователя (0 если еще не залогинился)
    uint8_t  read_buffer[SESSION_READ_BUFFER_SIZE];   // Буфер для входящих TCP пакетов
    size_t   read_bytes;                              // Общий объем данных в буфере чтения
    uint8_t  write_buffer[SESSION_WRITE_BUFFER_SIZE]; // Буфер для исходящих TCP пакетов
    size_t   write_offset;                            // Cколько байт уже отправлено
    size_t   write_bytes;                             // Общий объем данных в буфере записи
} ClientSession;

typedef SessionIoResult (*SessionFrameHandler)(void* context, ClientSession* session, const ParsedFrame* frame);

void session_init(ClientSession* session, int socket_fd);
void session_reset(ClientSession* session);

bool session_is_active(const ClientSession* session);
bool session_has_pending_write(const ClientSession* session);

SessionIoResult session_queue_frame(
    ClientSession* session,
    MessageType    type,
    uint32_t       request_id,
    const uint8_t* payload,
    uint32_t       payload_len
);

SessionIoResult session_handle_read(ClientSession* session, SessionFrameHandler frame_handler, void* handler_context);
SessionIoResult session_handle_write(ClientSession* session);

#endif

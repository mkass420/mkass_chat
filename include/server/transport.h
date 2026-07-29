#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "config.h"

#include "common/frame.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    TRANSPORT_IO_OK = 0,
    TRANSPORT_IO_PEER_CLOSED,
    TRANSPORT_IO_PROTOCOL_ERROR,
    TRANSPORT_IO_BUFFER_FULL,
    TRANSPORT_IO_SYSTEM_ERROR,
    TRANSPORT_IO_INVALID_ARGUMENT,
    TRANSPORT_IO_FRAME_BUILD_ERROR
} TransportIoResult;

typedef struct {
    int     socket_fd;                                 // Дескриптор сокета (<0 если не активен)
    uint8_t read_buffer[TRANSPORT_READ_BUFFER_SIZE];   // Буфер для входящих TCP пакетов
    size_t  read_bytes;                                // Общий объем данных в буфере чтения
    uint8_t write_buffer[TRANSPORT_WRITE_BUFFER_SIZE]; // Буфер для исходящих TCP пакетов
    size_t  write_offset;                              // Cколько байт уже отправлено
    size_t  write_bytes;                               // Общий объем данных в буфере записи
} TransportSession;

typedef TransportIoResult (*TransportFrameHandler)(void* context, const DecodedFrame* frame);

void transport_init(TransportSession* transport, int socket_fd);
void transport_reset(TransportSession* transport);

bool transport_is_active(const TransportSession* transport);
bool transport_has_pending_write(const TransportSession* transport);

TransportIoResult transport_queue_frame(
    TransportSession* transport,
    FrameCodec*       codec,
    MessageType       type,
    uint32_t          request_id,
    const uint8_t*    payload,
    uint32_t          payload_len
);

TransportIoResult transport_handle_read(
    TransportSession*     transport,
    FrameCodec*           codec,
    TransportFrameHandler handler,
    void*                 context
);

TransportIoResult transport_handle_write(TransportSession* transport);

#endif

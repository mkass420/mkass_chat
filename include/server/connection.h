#ifndef CLIENT_H
#define CLIENT_H

#include "server/file_transfer.h"
#include "server/transport.h"

#include <stdint.h>

typedef struct {
    uint64_t          generation; // Версия текущего подключения в слоте
    TransportSession  transport;  // TCP-транспорт подключения
    uint32_t          user_id;    // ID пользователя или 0 до авторизации
    FileTransferState files;      // Текущее состояния загрузки или выгрузки файлов пользователя
} ClientConnection;

void connection_slot_init(ClientConnection* connection);
void connection_open(ClientConnection* connection, int socket_fd);
void connection_reset(ClientConnection* connection);

bool connection_is_active(const ClientConnection* connection);

#endif

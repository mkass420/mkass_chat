#ifndef STATE_H
#define STATE_H

#include "config.h"
#include "server/connection.h"
#include "common/frame.h"

#include <sqlite3.h>

#include <stdint.h>
#include <stdbool.h>

// Глобальное состояние сервера
typedef struct {
    sqlite3*   db;            // Указатель на открытую БД
    int        epoll_fd;      // Дескриптор epoll для мультиплексирования
    int        server_socket; // Слушающий сокет сервера
    FrameCodec frame_codec;   // Кодек
    // FileStorage file_storage;
    ClientConnection connections[SERVER_MAX_CONNECTIONS]; // Массив со всеми активными TCP соединениями
} ServerState;

#endif

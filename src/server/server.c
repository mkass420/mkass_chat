#define _GNU_SOURCE

#include "server/server.h"

#include "server/dispatcher.h"
#include "server/transport.h"
#include "server/connection.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_EPOLL_MAX_EVENTS 64

// Флаг остановки, изменяемый обработчиком сигнала.
static volatile sig_atomic_t stop_requested = 0;

void server_request_stop(void) { stop_requested = 1; }

static ClientConnection* server_find_free_connection(ServerState* server) {
    for(size_t i = 0U; i < SERVER_MAX_CONNECTIONS; ++i) {
        ClientConnection* connection = &server->connections[i];

        if(!connection_is_active(connection)) {
            return connection;
        }
    }

    return NULL;
}

static void server_close_connection(ServerState* server, ClientConnection* connection) {
    if(server == NULL || connection == NULL || !connection_is_active(connection)) {
        return;
    }

    TransportSession* transport = &connection->transport;

    const int socket_fd = transport->socket_fd;

    // Удаляем сокет из epoll перед закрытием.
    if(server->epoll_fd >= 0) {
        (void)epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, socket_fd, NULL);
    }

    file_transfer_abort_all(&server->file_storage, &connection->files);
    (void)close(socket_fd);

    connection_reset(connection);
}

static int server_update_connection_events(ServerState* server, ClientConnection* connection) {
    TransportSession* transport = &connection->transport;

    struct epoll_event event = {0};

    event.events   = EPOLLIN | EPOLLRDHUP;
    event.data.ptr = connection;

    // EPOLLOUT нужен только при наличии данных в очереди.
    if(transport_has_pending_write(transport)) {
        event.events |= EPOLLOUT;
    }

    return epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, transport->socket_fd, &event);
}

static int server_add_client(ServerState* server, int client_fd) {
    ClientConnection* connection = server_find_free_connection(server);

    if(connection == NULL) {
        fprintf(stderr, "Connection rejected: connection limit reached\n");

        (void)close(client_fd);
        return 0;
    }

    connection_open(connection, client_fd);

    struct epoll_event event = {0};

    event.events   = EPOLLIN | EPOLLRDHUP;
    event.data.ptr = connection;

    if(epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &event) != 0) {
        const int saved_errno = errno;

        file_transfer_abort_all(&server->file_storage, &connection->files);
        (void)close(client_fd);
        connection_reset(connection);

        errno = saved_errno;
        return -1;
    }

    printf("Client connected: fd=%d, generation=%llu\n", client_fd, (unsigned long long)connection->generation);

    return 0;
}

static int server_accept_clients(ServerState* server) {
    for(;;) {
        const int client_fd = accept4(server->server_socket, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if(client_fd >= 0) {
            if(server_add_client(server, client_fd) != 0) {
                perror("epoll_ctl(ADD client)");
            }

            // Принимаем все накопившиеся подключения до EAGAIN.
            continue;
        }

        if(errno == EINTR) {
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }

        return -1;
    }
}

static const char* transport_io_result_to_string(TransportIoResult result) {
    switch(result) {
        case TRANSPORT_IO_OK: return "ok";
        case TRANSPORT_IO_PEER_CLOSED: return "peer closed connection";
        case TRANSPORT_IO_PROTOCOL_ERROR: return "protocol error";
        case TRANSPORT_IO_BUFFER_FULL: return "transport buffer is full";
        case TRANSPORT_IO_SYSTEM_ERROR: return "socket system error";
        case TRANSPORT_IO_INVALID_ARGUMENT: return "invalid transport argument";
        case TRANSPORT_IO_FRAME_BUILD_ERROR: return "frame build error";
        default: return "unknown transport error";
    }
}

static void server_handle_client_event(ServerState* server, ClientConnection* connection, uint32_t events) {
    if(!connection_is_active(connection)) {
        return;
    }

    TransportSession* transport = &connection->transport;

    if((events & (EPOLLERR | EPOLLHUP)) != 0U) {
        printf("Client disconnected: fd=%d, epoll error/hangup\n", transport->socket_fd);

        server_close_connection(server, connection);

        return;
    }

    TransportIoResult result = TRANSPORT_IO_OK;

    if((events & (EPOLLIN | EPOLLRDHUP)) != 0U) {
        ServerDispatchContext dispatch_context = {
            .server     = server,
            .connection = connection,
        };

        result = transport_handle_read(transport, &server->frame_codec, server_dispatch_frame, &dispatch_context);
    }

    // Сразу отправляем подготовленный ответ.
    if(result == TRANSPORT_IO_OK && transport_has_pending_write(transport)) {
        result = transport_handle_write(transport);
    }

    if(result != TRANSPORT_IO_OK) {
        const int socket_fd = transport->socket_fd;

        fprintf(stderr, "Client fd=%d closed: %s\n", socket_fd, transport_io_result_to_string(result));

        server_close_connection(server, connection);

        return;
    }

    if(server_update_connection_events(server, connection) != 0) {
        perror("epoll_ctl(MOD client)");

        server_close_connection(server, connection);
    }
}

int server_init(ServerState* server, const char* bind_address, uint16_t port) {
    if(server == NULL || port == 0U) {
        errno = EINVAL;
        return -1;
    }

    stop_requested = 0;

    server->db            = NULL;
    server->epoll_fd      = -1;
    server->server_socket = -1;

    file_storage_reset(&server->file_storage);

    for(size_t i = 0U; i < SERVER_MAX_CONNECTIONS; ++i) {
        connection_slot_init(&server->connections[i]);
    }

    const int listener_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);

    if(listener_fd < 0) {
        return -1;
    }

    const int reuse_address = 1;

    if(setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) != 0) {
        const int saved_errno = errno;

        (void)close(listener_fd);
        errno = saved_errno;
        return -1;
    }

    struct sockaddr_in address = {0};

    address.sin_family = AF_INET;
    address.sin_port   = htons(port);

    if(bind_address == NULL || bind_address[0] == '\0' || strcmp(bind_address, "0.0.0.0") == 0) {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else {
        const int parse_result = inet_pton(AF_INET, bind_address, &address.sin_addr);

        if(parse_result != 1) {
            const int saved_errno = parse_result == 0 ? EINVAL : errno;

            (void)close(listener_fd);
            errno = saved_errno;
            return -1;
        }
    }

    if(bind(listener_fd, (const struct sockaddr*)&address, sizeof(address)) != 0) {
        const int saved_errno = errno;

        (void)close(listener_fd);
        errno = saved_errno;
        return -1;
    }

    if(listen(listener_fd, SOMAXCONN) != 0) {
        const int saved_errno = errno;

        (void)close(listener_fd);
        errno = saved_errno;
        return -1;
    }

    const int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if(epoll_fd < 0) {
        const int saved_errno = errno;

        (void)close(listener_fd);
        errno = saved_errno;
        return -1;
    }

    struct epoll_event listener_event = {0};

    // NULL в data.ptr обозначает слушающий сокет.
    listener_event.events   = EPOLLIN;
    listener_event.data.ptr = NULL;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener_fd, &listener_event) != 0) {
        const int saved_errno = errno;

        (void)close(epoll_fd);
        (void)close(listener_fd);

        errno = saved_errno;
        return -1;
    }

    if(file_storage_init(&server->file_storage, "data/files") != 0) {
        const int saved_errno = errno;

        file_storage_destroy(&server->file_storage);

        (void)close(epoll_fd);
        (void)close(listener_fd);

        errno = saved_errno;
        return -1;
    }

    server->server_socket = listener_fd;
    server->epoll_fd      = epoll_fd;

    return 0;
}

int server_run(ServerState* server) {
    if(server == NULL || server->epoll_fd < 0 || server->server_socket < 0) {
        errno = EINVAL;
        return -1;
    }

    struct epoll_event events[SERVER_EPOLL_MAX_EVENTS];

    while(!stop_requested) {
        const int event_count = epoll_wait(server->epoll_fd, events, SERVER_EPOLL_MAX_EVENTS, -1);

        if(event_count < 0) {
            if(errno == EINTR) {
                // EINTR позволяет проверить флаг остановки.
                continue;
            }

            return -1;
        }

        for(int i = 0; i < event_count; ++i) {
            struct epoll_event* event = &events[i];

            if(event->data.ptr == NULL) {
                if(server_accept_clients(server) != 0) {
                    return -1;
                }

                continue;
            }

            ClientConnection* connection = event->data.ptr;
            server_handle_client_event(server, connection, event->events);
        }
    }

    return 0;
}

void server_destroy(ServerState* server) {
    if(server == NULL) {
        return;
    }

    for(size_t i = 0U; i < SERVER_MAX_CONNECTIONS; ++i) {
        server_close_connection(server, &server->connections[i]);
    }

    file_storage_destroy(&server->file_storage);

    if(server->server_socket >= 0) {
        (void)close(server->server_socket);
        server->server_socket = -1;
    }

    if(server->epoll_fd >= 0) {
        (void)close(server->epoll_fd);
        server->epoll_fd = -1;
    }

    // Базу данных позже закрывает отдельный модуль.
    server->db = NULL;
}

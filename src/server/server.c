#define _GNU_SOURCE

#include "server/server.h"

#include "server/dispatcher.h"
#include "server/session.h"

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

static ClientSession* server_find_free_session(ServerState* server) {
    for(size_t i = 0U; i < SERVER_MAX_CONNECTIONS; ++i) {
        ClientSession* session = &server->sessions[i];

        if(!session_is_active(session)) {
            return session;
        }
    }

    return NULL;
}

static void server_close_session(ServerState* server, ClientSession* session) {
    if(server == NULL || session == NULL || !session_is_active(session)) {
        return;
    }

    const int socket_fd = session->socket_fd;

    // Явно удаляем сокет из epoll перед закрытием.
    if(server->epoll_fd >= 0) {
        (void)epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, socket_fd, NULL);
    }

    (void)close(socket_fd);
    session_reset(session);
}

static int server_update_session_events(ServerState* server, ClientSession* session) {
    struct epoll_event event = {0};

    event.events   = EPOLLIN | EPOLLRDHUP;
    event.data.ptr = session;

    // EPOLLOUT нужен только при наличии данных в очереди.
    if(session_has_pending_write(session)) {
        event.events |= EPOLLOUT;
    }

    return epoll_ctl(server->epoll_fd, EPOLL_CTL_MOD, session->socket_fd, &event);
}

static int server_add_client(ServerState* server, int client_fd) {
    ClientSession* session = server_find_free_session(server);

    if(session == NULL) {
        fprintf(stderr, "Connection rejected: session limit reached\n");
        (void)close(client_fd);
        return 0;
    }

    session_init(session, client_fd);

    struct epoll_event event = {0};

    event.events   = EPOLLIN | EPOLLRDHUP;
    event.data.ptr = session;

    if(epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &event) != 0) {
        const int saved_errno = errno;

        (void)close(client_fd);
        session_reset(session);

        errno = saved_errno;
        return -1;
    }

    printf("Client connected: fd=%d\n", client_fd);
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

static const char* session_io_result_to_string(SessionIoResult result) {
    switch(result) {
        case SESSION_IO_OK: return "ok";
        case SESSION_IO_PEER_CLOSED: return "peer closed connection";
        case SESSION_IO_PROTOCOL_ERROR: return "protocol error";
        case SESSION_IO_BUFFER_FULL: return "session buffer is full";
        case SESSION_IO_SYSTEM_ERROR: return "socket system error";
        case SESSION_IO_INVALID_ARGUMENT: return "invalid session argument";
        case SESSION_IO_FRAME_BUILD_ERROR: return "frame build error";
        default: return "unknown session error";
    }
}

static void server_handle_client_event(ServerState* server, ClientSession* session, uint32_t events) {
    if(!session_is_active(session)) {
        return;
    }

    if((events & (EPOLLERR | EPOLLHUP)) != 0U) {
        printf("Client disconnected: fd=%d, epoll error/hangup\n", session->socket_fd);
        server_close_session(server, session);
        return;
    }

    SessionIoResult result = SESSION_IO_OK;

    // При EPOLLRDHUP дочитываем входной буфер до EOF.
    if((events & (EPOLLIN | EPOLLRDHUP)) != 0U) {
        result = session_handle_read(session, server_dispatch_frame, server);
    }

    // Сразу отправляем подготовленный ответ без ожидания EPOLLOUT.
    if(result == SESSION_IO_OK && session_has_pending_write(session)) {
        result = session_handle_write(session);
    }

    if(result != SESSION_IO_OK) {
        const int socket_fd = session->socket_fd;

        fprintf(stderr, "Client fd=%d closed: %s\n", socket_fd, session_io_result_to_string(result));
        server_close_session(server, session);
        return;
    }

    if(server_update_session_events(server, session) != 0) {
        perror("epoll_ctl(MOD client)");
        server_close_session(server, session);
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

    for(size_t i = 0U; i < SERVER_MAX_CONNECTIONS; ++i) {
        session_reset(&server->sessions[i]);
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

            ClientSession* session = event->data.ptr;
            server_handle_client_event(server, session, event->events);
        }
    }

    return 0;
}

void server_destroy(ServerState* server) {
    if(server == NULL) {
        return;
    }

    for(size_t i = 0U; i < SERVER_MAX_CONNECTIONS; ++i) {
        server_close_session(server, &server->sessions[i]);
    }

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

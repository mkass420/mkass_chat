#define _POSIX_C_SOURCE 200809L

#include "server/server.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_BIND_ADDRESS "0.0.0.0"
#define DEFAULT_SERVER_PORT  5555U

static void handle_stop_signal(int signal_number) {
    (void)signal_number;
    server_request_stop();
}

static int install_signal_handlers(void) {
    struct sigaction action = {0};

    action.sa_handler = handle_stop_signal;

    if(sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }

    // SA_RESTART не используется, чтобы прервать epoll_wait().
    action.sa_flags = 0;

    if(sigaction(SIGINT, &action, NULL) != 0) {
        return -1;
    }
    if(sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }

    return 0;
}

static int parse_port(const char* text, uint16_t* port) {
    if(text == NULL || port == NULL) {
        return -1;
    }

    char* end = NULL;
    errno = 0;

    const unsigned long value = strtoul(text, &end, 10);

    if(errno != 0 || end == text || *end != '\0' || value == 0UL || value > 65535UL) {
        return -1;
    }

    *port = (uint16_t)value;
    return 0;
}

static void print_usage(const char* program_name) {
    fprintf(
        stderr,
        "Usage: %s [IPv4 address] [port]\n"
        "\n"
        "Examples:\n"
        "  %s\n"
        "  %s 127.0.0.1 5555\n"
        "  %s 0.0.0.0 8080\n",
        program_name, program_name, program_name, program_name
    );
}

int main(int argc, char** argv) {
    if(argc > 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* bind_address = DEFAULT_BIND_ADDRESS;
    uint16_t port = DEFAULT_SERVER_PORT;

    if(argc >= 2) {
        bind_address = argv[1];
    }

    if(argc >= 3 && parse_port(argv[2], &port) != 0) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if(install_signal_handlers() != 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    // ServerState выделяется в куче из-за большого массива сессий.
    ServerState* server = calloc(1U, sizeof(*server));

    if(server == NULL) {
        perror("calloc ServerState");
        return EXIT_FAILURE;
    }

    if(server_init(server, bind_address, port) != 0) {
        perror("server_init");
        server_destroy(server);
        free(server);
        return EXIT_FAILURE;
    }

    // Включаем построчную буферизацию логов.
    (void)setvbuf(stdout, NULL, _IOLBF, 0);

    printf("Server listening on %s:%u\n", bind_address, (unsigned int)port);

    const int run_result = server_run(server);

    if(run_result != 0) {
        perror("server_run");
    }

    printf("Server shutting down\n");

    server_destroy(server);
    free(server);

    return run_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

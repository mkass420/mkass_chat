#include "server/connection.h"

void connection_reset(ClientConnection* connection) {
    assert(connection != NULL);

    transport_reset(&connection->transport);
    connection->user_id = 0U;
}

void connection_open(ClientConnection* connection, int socket_fd) {
    assert(connection != NULL);
    assert(socket_fd >= 0);

    connection_reset(connection);

    ++connection->generation;

    if(connection->generation == 0U) {
        connection->generation = 1U;
    }

    transport_init(&connection->transport, socket_fd);
}

void connection_slot_init(ClientConnection* connection) {
    assert(connection != NULL);

    connection->generation = 0U;
    connection->user_id    = 0U;

    transport_reset(&connection->transport);
}

bool connection_is_active(const ClientConnection* connection) {
    return connection != NULL && transport_is_active(&connection->transport);
}

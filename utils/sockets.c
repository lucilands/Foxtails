#include "server.h"
#include <asm-generic/errno.h>
#include <errno.h>
#include <sockets.h>
#include <workers.h>
#include <clog.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>


socket_t http_socket_create(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        clog(CLOG_FATAL, "Failed to create socket: %s", strerror(errno));
        exit(1);
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        clog(CLOG_FATAL, "Failed to set SO_REUSEADDR: %s", strerror(errno));
        close(fd);
        exit(1);
    }

    socket_t ret = {0};
    ret.fd = fd;
    ret.address.sin_family = AF_INET;
    ret.address.sin_addr.s_addr = INADDR_ANY;
    ret.address.sin_port = htons(port);

    if (bind(ret.fd, (struct sockaddr*)&ret.address, sizeof(ret.address))) {
        clog(CLOG_FATAL, "Failed to bind to port %i: %s", port, strerror(errno));
        close(ret.fd);
        exit(1);
    }

    int flags = fcntl(ret.fd, F_GETFL, 0);
    fcntl(ret.fd, F_SETFL, flags | O_NONBLOCK);

    clog(CLOG_TRACE, "Created listening socket fd=%d on port %i", ret.fd, port);
    return ret;
}

void http_socket_listen(socket_t sock, worker_pool_t worker_pool) {
    if (listen(sock.fd, worker_pool.num_workers) < 0) {
        clog(CLOG_FATAL, "Listen failed: %s", strerror(errno));
        exit(1);
    }
    clog(CLOG_TRACE, "Listening on fd=%d with backlog %u", sock.fd, worker_pool.num_workers);
}

bool http_socket_accept(socket_t sock, socket_t *client) {
    socklen_t addrlen = sizeof(client->address);
    client->fd = accept(sock.fd, (struct sockaddr*)&client->address, &addrlen);

    if (client->fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }

        clog(CLOG_FATAL, "Accept failed: %s", strerror(errno));
        exit(1);
    }

    int flags = fcntl(client->fd, F_GETFL, 0);
    fcntl(client->fd, F_SETFL, flags | O_NONBLOCK);

    return true;
}

void dispatch_client(worker_pool_t *pool, client_t *client) {
    dispatch_command(pool, WORKER_ACTION_NEW_CLIENT, client, sizeof(*client));
}

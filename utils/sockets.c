#include "server.h"
#include <assert.h>
#include <errno.h>
#include <errno.h>
#include <sockets.h>
#include <workers.h>
#include <clog.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>


socket_t socket_create_http(int port) {
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
    ret.address = malloc(sizeof(struct sockaddr_in));
    assert(ret.address);

    ((struct sockaddr_in*)ret.address)->sin_family = AF_INET;
    ((struct sockaddr_in*)ret.address)->sin_addr.s_addr = INADDR_ANY;
    ((struct sockaddr_in*)ret.address)->sin_port = htons(port);

    if (bind(ret.fd, ret.address, sizeof(*(struct sockaddr_in*)ret.address))) {
        clog(CLOG_FATAL, "Failed to bind to port %i: %s", port, strerror(errno));
        free(ret.address);
        close(ret.fd);
        exit(1);
    }

    int flags = fcntl(ret.fd, F_GETFL, 0);
    fcntl(ret.fd, F_SETFL, flags | O_NONBLOCK);

    clog(CLOG_TRACE, "Created listening socket fd=%d on port %i", ret.fd, port);
    return ret;
}

void socket_listen(socket_t sock, worker_pool_t worker_pool) {
    if (listen(sock.fd, worker_pool.num_workers) < 0) {
        clog(CLOG_FATAL, "Listen failed: %s", strerror(errno));
        exit(1);
    }
    clog(CLOG_TRACE, "Listening on fd=%d with backlog %u", sock.fd, worker_pool.num_workers);
}

bool socket_accept(socket_t sock, socket_t *client) {
    client->address = calloc(1, sizeof(struct sockaddr_in));
    assert(client->address);
    socklen_t addrlen = sizeof(struct sockaddr_in);
    client->fd = accept(sock.fd, client->address, &addrlen);

    if (client->fd < 0) {
        free(client->address);
        client->address = NULL;

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

socket_t socket_create_unix(char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
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
    ret.address = malloc(sizeof(struct sockaddr_un));
    assert(ret.address);

    unlink(path);
    memset(ret.address, 0, sizeof(struct sockaddr_un));
    ((struct sockaddr_un*)ret.address)->sun_family = AF_UNIX;
    strncpy(((struct sockaddr_un*)ret.address)->sun_path, path, sizeof(((struct sockaddr_un*)ret.address)->sun_path) - 1);

    if (bind(ret.fd, ret.address, sizeof(struct sockaddr_un))) {
        clog(CLOG_FATAL, "Failed to bind to path \"%s\": %s", path, strerror(errno));
        free(ret.address);
        close(ret.fd);
        exit(1);
    }

    int flags = fcntl(ret.fd, F_GETFL, 0);
    fcntl(ret.fd, F_SETFL, flags | O_NONBLOCK);

    clog(CLOG_TRACE, "Created listening socket fd=%d on path \"%s\"", ret.fd, path);
    return ret;
}

socket_t socket_create_auto(char *url) {
    if (strlen(url) < strlen("http://")) {
        clog(CLOG_ERROR, "URL to short");
        return (socket_t){0};
    }
    if (strncmp(url, "http://", strlen("http://")) == 0) {
        socket_t client = {0};

        client.fd = socket(AF_INET, SOCK_STREAM, 0);
        if (!client.fd) {
            clog(CLOG_ERROR, "Failed to connect to backend service at '%s'", url);
            return (socket_t){0};
        }

        client.address = malloc(sizeof(struct sockaddr_in));
        if (!client.address) {
            close(client.fd);
            clog(CLOG_ERROR, "Failed to allocate memory");
            return (socket_t){0};
        }

        char ip[INET_ADDRSTRLEN] = "";
        int port = 0;

        int items = sscanf(url, "http://%15[^:]:%i", ip, &port);
        if (items < 2) {
            clog(CLOG_ERROR, "Malformed URL '%s'", url);
            close(client.fd);
            free(client.address);
            return (socket_t){0};
        }

        struct sockaddr_in *addr = (struct sockaddr_in*)client.address;
        addr->sin_family = AF_INET;
        addr->sin_port = htons(port);

        if (inet_pton(AF_INET, ip, &addr->sin_addr) <= 0) {
            clog(CLOG_ERROR, "Invalid IP address");
            close(client.fd);
            free(client.address);
            return (socket_t){0};
        }

        if (connect(client.fd, client.address, sizeof(*addr)) < 0) {
            clog(CLOG_ERROR, "connect failed");
            close(client.fd);
            free(client.address);
            return (socket_t){0};
        }
        return client;
    }
    return (socket_t){0};
}

void dispatch_client(worker_pool_t *pool, client_t *client) {
    dispatch_command(pool, WORKER_ACTION_NEW_CLIENT, client, sizeof(*client));
}


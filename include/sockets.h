#ifndef __SOCKETS_H
#define __SOCKETS_H
#include <stdbool.h>
#include "workers.h"

typedef struct socket {
    int fd;
    struct sockaddr *address;
} socket_t;

struct client;

socket_t socket_create_http(int port);
void socket_listen(socket_t sock, worker_pool_t worker_pool);
bool socket_accept(socket_t sock, socket_t *client);
void dispatch_client(worker_pool_t *pool, struct client *client);

socket_t socket_create_unix(char *path);

#endif //__SOCKETS_H

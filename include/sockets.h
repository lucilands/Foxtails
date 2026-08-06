#ifndef __SOCKETS_H
#define __SOCKETS_H
#include <netinet/in.h>
#include <stdbool.h>
#include "workers.h"

typedef struct socket {
    int fd;
    struct sockaddr_in address;
} socket_t;

struct client;

socket_t http_socket_create(int port);
void http_socket_listen(socket_t sock, worker_pool_t worker_pool);
bool http_socket_accept(socket_t sock, socket_t *client);
void dispatch_client(worker_pool_t *pool, struct client *client);

#endif //__SOCKETS_H

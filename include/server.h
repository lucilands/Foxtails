#ifndef __SERVER_H
#define __SERVER_H
#include "sockets.h"
#include "workers.h"
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <netinet/in.h>


struct server;
typedef struct client {
    socket_t socket;
    time_t last_recv;
    int idx;
    struct server *serv;
    bool is_alive;
    bool in_flight;
    char ip_addr[INET_ADDRSTRLEN];
} client_t;

typedef struct {
    int *data;
    unsigned int head;
    unsigned int capacity;

    pthread_mutex_t lock;
} int_stack_t;

bool int_stack_push(int_stack_t *stack, int value);
int int_stack_pop(int_stack_t *stack);

typedef struct server {
    socket_t socket;
    worker_pool_t workers;
    client_t *clients;
    int epoll_instance;
    
    int_stack_t free_list;
} server_t;

server_t server_init_http(int max_connections, int num_workers, int port);
server_t server_init_unix(int max_connections, int num_workers, char *path);
void server_delete(server_t server);

void server_append_client(server_t *server, socket_t client, time_t oldest_client);
void server_remove_client(server_t *server, client_t client);

#endif //__SERVER_H

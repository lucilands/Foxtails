#ifndef __SERVER_H
#define __SERVER_H
#include "sockets.h"
#include "workers.h"
#include <pthread.h>
#include <stdbool.h>
#include <time.h>


struct server;
typedef struct client {
    socket_t socket;
    time_t last_recv;
    int idx;
    struct server *serv;
} client_t;

typedef struct {
    int *data;
    unsigned int head;
    unsigned int capacity;

    pthread_mutex_t lock;
} int_stack_t;

// Returns false (and logs) instead of writing past `stack->capacity`.
bool int_stack_push(int_stack_t *stack, int value);
// Returns -1 (and logs) instead of reading past an empty stack.
int int_stack_pop(int_stack_t *stack);

typedef struct server {
    socket_t socket;
    worker_pool_t workers;
    client_t *clients;
    int epoll_instance;
    
    int_stack_t free_list;
} server_t;

server_t server_init(int max_connections, int num_workers, int port);
void server_delete(server_t server);

void server_append_client(server_t *server, socket_t client);
void server_remove_client(server_t *server, client_t client);

#endif //__SERVER_H

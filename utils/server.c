#include <pthread.h>
#include <server.h>
#include <clog.h>
#include <cpool.h>

#include <unistd.h>
#include <sys/epoll.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>


bool int_stack_push(int_stack_t *stack, int value) {
    if (stack->head >= stack->capacity) {
        clog(CLOG_ERROR, "Free list overflow: attempted to push past capacity %u", stack->capacity);
        return false;
    }
    stack->data[stack->head++] = value;
    return true;
}

int int_stack_pop(int_stack_t *stack) {
    if (stack->head == 0) {
        clog(CLOG_WARNING, "Free list exhausted: no free client slots available");
        return -1;
    }
    return stack->data[--stack->head];
}

server_t server_init(int max_connections, int num_workers, int port) {
    server_t server = {0};

    clog(CLOG_INFO, "Starting worker pool with %i worker threads", num_workers);
    server.workers = worker_pool_init(num_workers);

    server.socket = http_socket_create(port);
    clog(CLOG_TRACE, "Created socket on port %i", port);

    cpool_align(__alignof__(client_t));
    server.clients = pcalloc(max_connections, sizeof(client_t));
    if (!server.clients) {
        clog(CLOG_FATAL, "Failed to allocate memory for %i clients", max_connections);
        exit(1);
    }

    server.epoll_instance = epoll_create1(0);
    if (server.epoll_instance < 0) {
        clog(CLOG_FATAL, "Failed to create epoll instance: %s", strerror(errno));
        exit(1);
    }
    clog(CLOG_TRACE, "Created epoll instance (fd=%i)", server.epoll_instance);

    struct epoll_event listen_event = {
        .events = EPOLLIN,
        .data = {.ptr = NULL}
    };
    if (epoll_ctl(server.epoll_instance, EPOLL_CTL_ADD, server.socket.fd, &listen_event)) {
        clog(CLOG_FATAL, "Failed to register listening socket with epoll: %s", strerror(errno));
        exit(1);
    }
    clog(CLOG_TRACE, "Registered listening socket with epoll");

    cpool_align(__alignof__(int));
    server.free_list.data = pcalloc(max_connections, sizeof(int));
    if (!server.free_list.data) {
        clog(CLOG_FATAL, "Failed to allocate memory for %i clients", max_connections);
        exit(1);
    }
    server.free_list.capacity = max_connections;
    for (int i = max_connections-1; i >= 0; i--) {
        if (!int_stack_push(&server.free_list, i)) {
            clog(CLOG_FATAL, "Failed to initialize client free list");
            exit(1);
        }
    }
    clog(CLOG_TRACE, "Initialized client free list with %i slots", max_connections);

    pthread_mutex_init(&server.free_list.lock, NULL);

    return server;
}

void server_append_client(server_t *server, socket_t client) {
    pthread_mutex_lock(&server->free_list.lock);
    int free_idx = int_stack_pop(&server->free_list);
    if (free_idx < 0) {
        close(client.fd);
        clog(CLOG_WARNING, "Client pool exhausted (capacity=%u); rejecting fd=%d", server->free_list.capacity, client.fd);
        pthread_mutex_unlock(&server->free_list.lock);
        return;
    }

    server->clients[free_idx] = (client_t) {
        .socket = client,
        .last_recv = time(NULL),
        .serv = server,
        .idx = free_idx
    };
    struct epoll_event event = {
        .events = EPOLLIN | EPOLLONESHOT,
        .data = {.ptr = &server->clients[free_idx]}
    };
    int result = epoll_ctl(server->epoll_instance, EPOLL_CTL_ADD, client.fd, &event);
    if (result) {
        close(client.fd);
        int_stack_push(&server->free_list, free_idx);
        clog(CLOG_WARNING, "Failed to register fd=%d with epoll: %s. Client will be ignored", client.fd, strerror(errno));
        pthread_mutex_unlock(&server->free_list.lock);
        return;
    }
    server->clients[free_idx].is_alive = true;
    clog(CLOG_DEBUG, "Accepted client fd=%d into slot %d", client.fd, free_idx);
    pthread_mutex_unlock(&server->free_list.lock);
}

void server_remove_client(server_t *server, client_t client) {
    pthread_mutex_lock(&server->free_list.lock);
    if (epoll_ctl(server->epoll_instance, EPOLL_CTL_DEL, client.socket.fd, NULL)) {
        clog(CLOG_WARNING, "Failed to unregister fd=%d from epoll: %s", client.socket.fd, strerror(errno));
    }
    int_stack_push(&server->free_list, client.idx);
    close(client.socket.fd);
    clog(CLOG_DEBUG, "Removed client fd=%d from slot %d", client.socket.fd, client.idx);
    server->clients[client.idx].is_alive = false;
    pthread_mutex_unlock(&server->free_list.lock);
}

void server_delete(server_t server) {
    clog(CLOG_INFO, "Shutting down server");
    pthread_mutex_destroy(&server.free_list.lock);
    close(server.socket.fd);
    close(server.epoll_instance);
    worker_pool_delete(server.workers);
    clog(CLOG_INFO, "Server shut down");
}

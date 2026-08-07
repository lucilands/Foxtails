#ifndef __WORKERS_H
#define __WORKERS_H
#include <pthread.h>

#include <stdbool.h>
#include <stddef.h>

#define CLOG_PRETTY_FMT "%c[%L]%r %t %f:%l   %m"

#define WORKER_ACTION_PAYLOAD_SIZE 128

enum {
    WORKER_ACTION_NOOP = 0,
    WORKER_ACTION_NEW_CLIENT,
};

typedef struct {
    bool lock;
    bool exists;
    int type;    // Value from WORKER_ACTION_* enum
    unsigned char payload[WORKER_ACTION_PAYLOAD_SIZE];
} worker_action_t;

struct worker_queue {
    worker_action_t *data;
    unsigned int head;
    unsigned int count;
    unsigned int capacity;
    bool shutting_down;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
};

typedef struct {
    pthread_t *workers;
    unsigned int num_workers;

    struct worker_queue *queue;
} worker_pool_t;

typedef struct {
    struct worker_queue *queue;
    unsigned int id;
} worker_thread_args_t;

worker_pool_t worker_pool_init(unsigned int num_workers);
void worker_pool_delete(worker_pool_t pool);

void dispatch_command(worker_pool_t *pool, int action, void *payload, size_t payload_size);
void worker_callback(void *payload, int type);

#endif //__WORKERS_H

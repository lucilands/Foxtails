#include <assert.h>
#include <clog.h>
#include <cpool.h>
#include <pthread.h>
#include <sys/socket.h>
#include <workers.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


void *worker_loop(void *arg) {
    worker_thread_args_t *targs = (worker_thread_args_t*)arg;
    struct worker_queue *queue = targs->queue;
    unsigned int worker_id = targs->id;
    free(targs);

    char fmt_buf[128];
    snprintf(fmt_buf, sizeof(fmt_buf), "[Worker %d] %s", worker_id, CLOG_PRETTY_FMT);
    clog_set_fmt(fmt_buf);

    cpool_t mempool = cpool_init(1024);

    clog(CLOG_TRACE, "Started worker thread");
    while (1) {
        cpool_save();
        pthread_mutex_lock(&queue->lock);
        pthread_cond_wait(&queue->not_empty, &queue->lock);

        if (queue->shutting_down) {
            clog(CLOG_TRACE, "Worker shutdown");
            pthread_mutex_unlock(&queue->lock);
            break;
        }

        if (queue->count == 0) {
            pthread_mutex_unlock(&queue->lock);
            continue;
        }

        unsigned int idx = queue->head;
        queue->data[idx].lock = true;
        queue->data[idx].exists = false;
        queue->head = (queue->head + 1) % queue->capacity;
        queue->count--;

        clog(CLOG_TRACE, "Worker %u picked up action type=%d (queue depth now %u)", worker_id, queue->data[idx].type, queue->count);
        worker_callback(queue->data[idx].payload, queue->data[idx].type);
        queue->data[idx].lock = false;
        pthread_mutex_unlock(&queue->lock);
        cpool_restore();
    }

    clog(CLOG_TRACE, "Worker thread exiting");
    cpool_uninit(mempool);
    return NULL;
}

void init_queue(struct worker_queue **queue) {
    (*queue)->data = malloc(8 * sizeof(worker_action_t));
    if (!(*queue)->data) {
        clog(CLOG_FATAL, "Failed to allocate worker queue");
        exit(1);
    }
    memset((*queue)->data, 0, 8 * sizeof(worker_action_t));
    (*queue)->capacity = 8;
    (*queue)->head = 0;
    (*queue)->count = 0;
    (*queue)->shutting_down = false;

    pthread_mutex_init(&(*queue)->lock, NULL);
    pthread_cond_init(&(*queue)->not_empty, NULL);
}

worker_pool_t worker_pool_init(unsigned int num_workers) {
    worker_pool_t ret = {0};
    ret.workers = malloc(sizeof(pthread_t) * num_workers);
    if (!ret.workers) {
        clog(CLOG_FATAL, "Failed to allocate thread pool");
        exit(1);
    }

    ret.num_workers = num_workers;

    ret.queue = malloc(sizeof(*ret.queue));
    if (!ret.queue) {
        clog(CLOG_FATAL, "Failed to allocate worker queue");
        exit(1);
    }

    init_queue(&ret.queue);

    pthread_mutex_lock(&ret.queue->lock);
    for (unsigned int i = 0; i < ret.num_workers; i++) {
        worker_thread_args_t *targs = malloc(sizeof(*targs));
        if (!targs) {
            clog(CLOG_FATAL, "Failed to allocate worker thread args");
            exit(1);
        }
        targs->queue = ret.queue;
        targs->id = i;
        int err = pthread_create(&ret.workers[i], NULL, worker_loop, targs);
        if (err) {
            clog(CLOG_FATAL, "Failed to create worker thread %u: %s", i, strerror(err));
            exit(1);
        }
    }
    pthread_mutex_unlock(&ret.queue->lock);

    clog(CLOG_TRACE, "All %u worker threads started", ret.num_workers);
    return ret;
}

void worker_pool_delete(worker_pool_t pool) {
    clog(CLOG_INFO, "Shutting down worker pool (%u workers)", pool.num_workers);

    pthread_mutex_lock(&pool.queue->lock);
    pool.queue->shutting_down = true;
    pthread_mutex_unlock(&pool.queue->lock);
    pthread_cond_broadcast(&pool.queue->not_empty);

    for (unsigned int i = 0; i < pool.num_workers; i++) {
        pthread_join(pool.workers[i], NULL);
    }
    clog(CLOG_TRACE, "All worker threads joined");

    pthread_mutex_destroy(&pool.queue->lock);
    pthread_cond_destroy(&pool.queue->not_empty);
    free(pool.workers);
    if (pool.queue->data) free(pool.queue->data);
    free(pool.queue);

    clog(CLOG_INFO, "Worker pool shut down");
}

void dispatch_command(worker_pool_t *pool, int action, void *payload, size_t payload_size) {
    assert(payload_size <= WORKER_ACTION_PAYLOAD_SIZE);
    pthread_mutex_lock(&pool->queue->lock);

    if (pool->queue->count+1 >= pool->queue->capacity) {
        unsigned int old_capacity = pool->queue->capacity;
        unsigned int new_capacity = old_capacity * 2;
        worker_action_t *new_data = malloc(new_capacity * sizeof(worker_action_t));
        if (!new_data) {
            clog(CLOG_ERROR, "Failed to resize command queue from %u to %u slots", old_capacity, new_capacity);
            pthread_mutex_unlock(&pool->queue->lock);
            return;
        }

        for (unsigned int i = 0; i < pool->queue->count; i++) {
            new_data[i] = pool->queue->data[(pool->queue->head + i) % pool->queue->capacity];
        }

        free(pool->queue->data);
        pool->queue->data = new_data;
        pool->queue->head = 0;
        pool->queue->capacity = new_capacity;
        clog(CLOG_WARNING, "Command queue resized %u -> %u slots (consider raising initial capacity)", old_capacity, new_capacity);
    }

    unsigned int tail = (pool->queue->head + pool->queue->count) % pool->queue->capacity;
    worker_action_t *slot = &pool->queue->data[tail];
    slot->exists = true;
    slot->lock = false;
    slot->type = action;
    if (payload && payload_size > 0) {
        memcpy(slot->payload, payload, payload_size);
    }
    pool->queue->count++;

    pthread_mutex_unlock(&pool->queue->lock);
    pthread_cond_signal(&pool->queue->not_empty);
}

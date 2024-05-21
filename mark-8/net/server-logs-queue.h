#pragma once

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "server-log.h"

enum { SERVER_LOGS_QUEUE_MAX_SIZE = 8 };

struct ServerLogsQueue {
    ServerLog array[SERVER_LOGS_QUEUE_MAX_SIZE];
    size_t size;
    pthread_mutex_t queue_access_mutex;
    sem_t added_elems_sem;
    sem_t free_elems_sem;
    size_t read_index;
    size_t write_index;
};

static inline bool init_server_logs_queue(struct ServerLogsQueue* queue) {
    memset(queue, 0, sizeof(*queue));
    int err_code            = 0;
    const char* error_cause = "";
    err_code                = pthread_mutex_init(&queue->queue_access_mutex, NULL);
    if (err_code != 0) {
        error_cause = "pthread_mutex_init";
        errno       = err_code;
        goto init_server_logs_queue_empty_cleanup;
    }
    if (sem_init(&queue->added_elems_sem, true, 0) == -1) {
        error_cause = "sem_init";
        goto init_server_logs_queue_mutex_cleanup;
    }
    if (sem_init(&queue->free_elems_sem, true, SERVER_LOGS_QUEUE_MAX_SIZE) == -1) {
        error_cause = "sem_init";
        goto init_server_logs_queue_mutex_sem_1_cleanup;
    }

    return true;

init_server_logs_queue_mutex_sem_1_cleanup:
    sem_destroy(&queue->added_elems_sem);
init_server_logs_queue_mutex_cleanup:
    pthread_mutex_destroy(&queue->queue_access_mutex);
init_server_logs_queue_empty_cleanup:
    perror(error_cause);
    return false;
}

static inline void deinit_server_logs_queue(struct ServerLogsQueue* queue) {
    sem_destroy(&queue->free_elems_sem);
    sem_destroy(&queue->added_elems_sem);
    pthread_mutex_destroy(&queue->queue_access_mutex);
}

static inline bool server_logs_queue_nonblocking_enqueue(struct ServerLogsQueue* queue,
                                                         const ServerLog* log) {
    if (sem_trywait(&queue->free_elems_sem) == -1) {
        if (errno != EAGAIN) {
            perror("sem_trywait[server_logs_queue_nonblocking_enqueue]");
        }
        return false;
    }
    if (pthread_mutex_trylock(&queue->queue_access_mutex) != 0) {
        if (errno != EBUSY) {
            perror("pthread_mutex_trylock[server_logs_queue_nonblocking_enqueue]");
        }
        sem_post(&queue->free_elems_sem);
        return false;
    }

    size_t write_index = queue->write_index;
    memcpy(queue->array[write_index].message, log->message, sizeof(log->message));
    queue->write_index = (write_index + 1) % SERVER_LOGS_QUEUE_MAX_SIZE;
    assert(queue->size < SERVER_LOGS_QUEUE_MAX_SIZE);
    queue->size++;

    if (sem_post(&queue->added_elems_sem) == -1) {
        perror("sem_post[server_logs_queue_nonblocking_enqueue]");
    }
    if (pthread_mutex_unlock(&queue->queue_access_mutex)) {
        perror("pthread_mutex_unlock[server_logs_queue_nonblocking_enqueue]");
    }
    return true;
}

static inline bool server_logs_queue_dequeue(struct ServerLogsQueue* queue, ServerLog* log) {
    if (sem_wait(&queue->added_elems_sem) == -1) {
        perror("sem_wait[server_logs_queue_dequeue]");
        return false;
    }
    if (pthread_mutex_lock(&queue->queue_access_mutex) != 0) {
        perror("pthread_mutex_trylock[server_logs_queue_dequeue]");
        sem_post(&queue->added_elems_sem);
        return false;
    }

    size_t read_index = queue->read_index;
    memcpy(log->message, queue->array[read_index].message, sizeof(log->message));
    queue->read_index = (read_index + 1) % SERVER_LOGS_QUEUE_MAX_SIZE;
    assert(queue->size > 0);
    queue->size--;

    if (sem_post(&queue->free_elems_sem) == -1) {
        perror("sem_post[server_logs_queue_dequeue]");
    }
    if (pthread_mutex_unlock(&queue->queue_access_mutex)) {
        perror("pthread_mutex_unlock[server_logs_queue_dequeue]");
    }

    return true;
}

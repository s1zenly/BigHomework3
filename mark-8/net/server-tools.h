#pragma once

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <errno.h>      
#include <netinet/in.h>  
#include <pthread.h>     
#include <stdatomic.h>   
#include <stdbool.h>     
#include <stdint.h>      
#include <stdio.h>      
#include <stdlib.h>     

#include "net-config.h"
#include "server-log.h"
#include "server-logs-queue.h"

enum {
    MAX_NUMBER_OF_FIRST_WORKERS   = 3,
    MAX_NUMBER_OF_SECOND_WORKERS  = 5,
    MAX_NUMBER_OF_THIRD_WORKERS   = 2,
    MAX_NUMBER_OF_LOGS_COLLECTORS = 6,

    MAX_WORKERS_PER_SERVER =
        MAX_NUMBER_OF_FIRST_WORKERS + MAX_NUMBER_OF_SECOND_WORKERS + MAX_NUMBER_OF_THIRD_WORKERS,
    MAX_CONNECTIONS_PER_SERVER = MAX_WORKERS_PER_SERVER + MAX_NUMBER_OF_LOGS_COLLECTORS,
};

typedef struct ClientMetaInfo {
    char host[48];
    char port[16];
    char numeric_host[48];
    char numeric_port[16];
} ClientMetaInfo;

typedef struct Server {
    atomic_int sock_fd;
    struct sockaddr_in sock_addr;

    pthread_mutex_t first_workers_mutex;
    struct sockaddr_in first_workers_addrs[MAX_NUMBER_OF_FIRST_WORKERS];
    int first_workers_fds[MAX_NUMBER_OF_FIRST_WORKERS];
    struct ClientMetaInfo first_workers_info[MAX_NUMBER_OF_FIRST_WORKERS];
    volatile size_t first_workers_arr_size;

    pthread_mutex_t second_workers_mutex;
    struct sockaddr_in second_workers_addrs[MAX_NUMBER_OF_SECOND_WORKERS];
    int second_workers_fds[MAX_NUMBER_OF_SECOND_WORKERS];
    struct ClientMetaInfo second_workers_info[MAX_NUMBER_OF_SECOND_WORKERS];
    volatile size_t second_workers_arr_size;

    pthread_mutex_t third_workers_mutex;
    struct sockaddr_in third_workers_addrs[MAX_NUMBER_OF_THIRD_WORKERS];
    int third_workers_fds[MAX_NUMBER_OF_THIRD_WORKERS];
    struct ClientMetaInfo third_workers_info[MAX_NUMBER_OF_THIRD_WORKERS];
    volatile size_t third_workers_arr_size;

    pthread_mutex_t logs_collectors_mutex;
    struct sockaddr_in logs_collectors_addrs[MAX_NUMBER_OF_LOGS_COLLECTORS];
    int logs_collectors_fds[MAX_NUMBER_OF_LOGS_COLLECTORS];
    struct ClientMetaInfo logs_collectors_info[MAX_NUMBER_OF_LOGS_COLLECTORS];
    volatile size_t logs_collectors_arr_size;

    struct ServerLogsQueue logs_queue;
} Server[1];

bool init_server(Server server, uint16_t server_port);
void deinit_server(Server server);

bool server_accept_client(Server server, ClientType* type, size_t* insert_index);
bool nonblocking_enqueue_log(Server server, const ServerLog* log);
bool dequeue_log(Server server, ServerLog* log);
void send_server_log(Server server, const ServerLog* log);
void send_shutdown_signal_to_one_client(const Server server, ClientType type, size_t index);
void send_shutdown_signal_to_first_workers(Server server);
void send_shutdown_signal_to_second_workers(Server server);
void send_shutdown_signal_to_third_workers(Server server);
void send_shutdown_signal_to_logs_collectors(Server server);
void send_shutdown_signal_to_all_clients_of_type(Server server, ClientType type);
void send_shutdown_signal_to_all(Server server);

bool nonblocking_poll_workers_on_the_first_stage(Server server, PinsQueue pins_1_to_2);
bool nonblocking_poll_workers_on_the_second_stage(Server server, PinsQueue pins_1_to_2,
                                                  PinsQueue pins_2_to_3);
bool nonblocking_poll_workers_on_the_third_stage(Server server, PinsQueue pins_2_to_3);

static inline bool server_lock_mutex(pthread_mutex_t* mutex) {
    int err = pthread_mutex_lock(mutex);
    if (err != 0) {
        errno = err;
        perror("pthread_mutex_lock");
    }
    return err == 0;
}
static inline bool server_unlock_mutex(pthread_mutex_t* mutex) {
    int err = pthread_mutex_unlock(mutex);
    if (err != 0) {
        errno = err;
        perror("pthread_mutex_unlock");
    }
    return err == 0;
}
static inline bool server_lock_first_mutex(Server server) {
    return server_lock_mutex(&server->first_workers_mutex);
}
static inline bool server_unlock_first_mutex(Server server) {
    return server_unlock_mutex(&server->first_workers_mutex);
}
static inline bool server_lock_second_mutex(Server server) {
    return server_lock_mutex(&server->second_workers_mutex);
}
static inline bool server_unlock_second_mutex(Server server) {
    return server_unlock_mutex(&server->second_workers_mutex);
}
static inline bool server_lock_third_mutex(Server server) {
    return server_lock_mutex(&server->third_workers_mutex);
}
static inline bool server_unlock_third_mutex(Server server) {
    return server_unlock_mutex(&server->third_workers_mutex);
}
static inline bool server_lock_logs_collectors_mutex(Server server) {
    return server_lock_mutex(&server->logs_collectors_mutex);
}
static inline bool server_unlock_logs_collectors_mutex(Server server) {
    return server_unlock_mutex(&server->logs_collectors_mutex);
}

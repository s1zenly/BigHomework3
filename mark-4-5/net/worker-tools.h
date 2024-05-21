#pragma once

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <errno.h>       
#include <math.h>        
#include <netinet/in.h>  
#include <stdbool.h>     
#include <stdint.h>      
#include <stdio.h>       
#include <stdlib.h>      
#include <sys/socket.h>  
#include <unistd.h>      

#include "../util/config.h"  
#include "net-config.h"      
#include "pin.h"            

typedef enum WorkerType {
    FIRST_STAGE_WORKER  = 1,
    SECOND_STAGE_WORKER = 2,
    THIRD_STAGE_WORKER  = 3,
} WorkerType;

static inline const char* worker_type_to_string(WorkerType type) {
    switch (type) {
        case FIRST_STAGE_WORKER:
            return "first stage worker";
        case SECOND_STAGE_WORKER:
            return "second stage worker";
        case THIRD_STAGE_WORKER:
            return "third stage worker";
        default:
            return "unknown stage worker";
    }
}

typedef struct Worker {
    int worker_sock_fd;
    WorkerType type;
    struct sockaddr_in server_sock_addr;
} Worker[1];

bool init_worker(Worker worker, const char* server_ip, uint16_t server_port, WorkerType type);
void deinit_worker(Worker worker);

bool worker_should_stop(const Worker worker);
void print_sock_addr_info(const struct sockaddr* address, socklen_t sock_addr_len);
static inline void print_worker_info(Worker worker) {
    print_sock_addr_info((const struct sockaddr*)&worker->server_sock_addr,
                         sizeof(worker->server_sock_addr));
}
static inline void worker_handle_shutdown_signal() {
    printf("Received shutdown signal from the server\n");
}
static inline void handle_errno(const char* cause) {
    uint32_t errno_val = (uint32_t)(errno);
    switch (errno_val) {
        case EAGAIN:
        case ENETDOWN:
        case ENETUNREACH:
        case ENETRESET:
        case ECONNABORTED:
        case ECONNRESET:
        case ECONNREFUSED:
        case EHOSTDOWN:
        case EHOSTUNREACH:
            printf(
                "+---------------------------------------------+\n"
                "| Server stopped connection. Code: %-10u |\n"
                "+---------------------------------------------+\n",
                errno_val);
            break;
        default:
            perror(cause);
            break;
    }
}

static inline void handle_recvfrom_error(const char* bytes, ssize_t read_bytes) {
    if (read_bytes > 0) {
        if (is_shutdown_message(bytes, (size_t)read_bytes)) {
            printf(
                "+------------------------------------------+\n"
                "| Received shutdown signal from the server |\n"
                "+------------------------------------------+\n");
            return;
        }

        fprintf(stderr, "Read %zu unknown bytes: \"%.*s\"\n", (size_t)read_bytes, (int)read_bytes,
                bytes);
    }

    handle_errno("recvfrom");
}
static inline Pin receive_new_pin() {
    Pin pin = {.pin_id = rand()};
    return pin;
}
static inline bool check_pin_crookness(Pin pin) {
    uint32_t sleep_time = (uint32_t)rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME) + MIN_SLEEP_TIME;
    sleep(sleep_time);

    uint32_t x = (uint32_t)pin.pin_id;
#if defined(__GNUC__)
    return __builtin_parity(x) & 1;
#else
    return x & 1;
#endif
}
static inline bool send_pin(int sock_fd, const struct sockaddr_in* sock_addr, Pin pin) {
    union {
        char bytes[NET_BUFFER_SIZE];
        Pin pin;
    } buffer;
    buffer.pin   = pin;
    bool success = sendto(sock_fd, buffer.bytes, sizeof(pin), 0, (const struct sockaddr*)sock_addr,
                          sizeof(*sock_addr)) == sizeof(pin);
    if (!success) {
        handle_errno("sendto");
    }
    return success;
}
static inline bool send_not_croocked_pin(Worker worker, Pin pin) {
    return send_pin(worker->worker_sock_fd, &worker->server_sock_addr, pin);
}
static inline bool receive_pin(int sock_fd, Pin* rec_pin) {
    union {
        char bytes[NET_BUFFER_SIZE];
        Pin pin;
    } buffer           = {0};
    ssize_t read_bytes = recv(sock_fd, buffer.bytes, sizeof(*rec_pin), 0);
    if (read_bytes != sizeof(*rec_pin)) {
        handle_recvfrom_error(buffer.bytes, read_bytes);
        return false;
    }

    *rec_pin = buffer.pin;
    return true;
}
static inline bool receive_not_crooked_pin(Worker worker, Pin* rec_pin) {
    return receive_pin(worker->worker_sock_fd, rec_pin);
}
static inline void sharpen_pin(Pin pin) {
    (void)pin;
    uint32_t sleep_time = (uint32_t)rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME) + MIN_SLEEP_TIME;
    sleep(sleep_time);
}
static inline bool send_sharpened_pin(Worker worker, Pin pin) {
    return send_pin(worker->worker_sock_fd, &worker->server_sock_addr, pin);
}
static inline bool receive_sharpened_pin(Worker worker, Pin* rec_pin) {
    return receive_pin(worker->worker_sock_fd, rec_pin);
}
static inline bool check_sharpened_pin_quality(Pin sharpened_pin) {
    uint32_t sleep_time = (uint32_t)rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME) + MIN_SLEEP_TIME;
    sleep(sleep_time);
    return cos(sharpened_pin.pin_id) >= 0;
}

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <assert.h>      
#include <errno.h>       
#include <pthread.h>     
#include <signal.h>     
#include <stdbool.h>     
#include <stdint.h>      
#include <stdio.h>       
#include <stdlib.h>      
#include <string.h>      
#include <sys/socket.h>  
#include <time.h>        
#include <unistd.h>      

#include "../util/config.h"  
#include "../util/parser.h"  
#include "net-config.h"     
#include "pin.h"             
#include "server-tools.h"    
#include "worker-tools.h"    


static struct Server server              = {0};
static volatile bool is_acceptor_running = true;
static volatile bool is_poller_running   = true;

static void signal_handler(int sig) {
    is_acceptor_running = false;
    is_poller_running   = false;
    fprintf(stderr, "> Received signal %d\n", sig);
}

static void setup_signal_handler() {
    const int handled_signals[] = {
        SIGABRT, SIGINT, SIGTERM, SIGSEGV, SIGQUIT, SIGKILL,
    };
    for (size_t i = 0; i < sizeof(handled_signals) / sizeof(handled_signals[0]); i++) {
        signal(handled_signals[i], signal_handler);
    }
}

typedef enum HandleResult {
    EMPTY_WORKER_SOCKET,
    DEAD_WORKER_SOCKET,
    UNKNOWN_SOCKET_ERROR,
} HandleResult;

static HandleResult handle_nonblocking_error(const char* cause) {
    switch (errno) {
        case EAGAIN:
            return EMPTY_WORKER_SOCKET;
        case ENETDOWN:
        case ENETUNREACH:
        case ENETRESET:
        case ECONNABORTED:
        case ECONNRESET:
        case ECONNREFUSED:
        case EHOSTDOWN:
        case EHOSTUNREACH:
            return DEAD_WORKER_SOCKET;
        default:
            perror(cause);
            return UNKNOWN_SOCKET_ERROR;
    }
}

static bool is_socket_alive(int sock_fd) {
    int error     = 0;
    socklen_t len = sizeof(error);
    int retval    = getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (retval != 0) {
        fprintf(stderr, "> Error getting socket error code: %s\n", strerror(retval));
        return false;
    }
    if (error != 0) {
        fprintf(stderr, "> Socket error: %s\n", strerror(error));
        return false;
    }
    return true;
}

static bool poll_workers(int workers_fds[], const WorkerMetainfo workers_info[],
                         volatile size_t* current_workers_online, size_t max_number_of_workers,
                         PinsQueue pins_q) {
    for (size_t i = 0;
         i < max_number_of_workers && *current_workers_online > 0 && !pins_queue_full(pins_q);
         i++) {
        int worker_fd = workers_fds[i];
        if (worker_fd == -1) {
            continue;
        }
        if (!is_socket_alive(worker_fd)) {
            workers_fds[i] = -1;
            close(worker_fd);
            (*current_workers_online)--;
            continue;
        }

        union {
            char bytes[NET_BUFFER_SIZE];
            Pin pin;
        } buffer                = {0};
        const ssize_t read_size = recv(worker_fd, buffer.bytes, sizeof(Pin), MSG_DONTWAIT);
        if (read_size != sizeof(Pin)) {
            switch (handle_nonblocking_error("recv")) {
                case EMPTY_WORKER_SOCKET:
                    continue;
                case DEAD_WORKER_SOCKET:
                    workers_fds[i] = -1;
                    close(worker_fd);
                    (*current_workers_online)--;
                    continue;
                case UNKNOWN_SOCKET_ERROR:
                default:
                    return false;
            }
        }

        const WorkerMetainfo* info = &workers_info[i];
        printf(
            "> Received pin[pin_id=%d] from the "
            "worker[address=%s:%s | %s:%s]\n",
            buffer.pin.pin_id, info->host, info->port, info->numeric_host, info->numeric_port);
        bool res = pins_queue_try_put(pins_q, buffer.pin);
        assert(res);
    }

    return true;
}

static bool send_pins_to_workers(int workers_fds[], const WorkerMetainfo workers_info[],
                                 volatile size_t* current_workers_online,
                                 size_t max_number_of_workers, PinsQueue pins_q) {
    for (size_t i = 0;
         i < max_number_of_workers && *current_workers_online > 0 && !pins_queue_empty(pins_q);
         i++) {
        int worker_fd = workers_fds[i];
        if (worker_fd == -1) {
            continue;
        }

        if (!is_socket_alive(worker_fd)) {
            workers_fds[i] = -1;
            close(worker_fd);
            (*current_workers_online)--;
            continue;
        }

        union {
            char bytes[sizeof(Pin)];
            Pin pin;
        } buffer;
        Pin pin;
        bool res = pins_queue_try_pop(pins_q, &pin);
        assert(res);
        buffer.pin = pin;

        const ssize_t sent_size = send(worker_fd, buffer.bytes, sizeof(Pin), MSG_DONTWAIT);
        if (sent_size != sizeof(Pin)) {
            switch (handle_nonblocking_error("send")) {
                case EMPTY_WORKER_SOCKET:
                    continue;
                case DEAD_WORKER_SOCKET:
                    workers_fds[i] = -1;
                    close(worker_fd);
                    (*current_workers_online)--;
                    continue;
                case UNKNOWN_SOCKET_ERROR:
                default:
                    return false;
            }
        }

        const WorkerMetainfo* info = &workers_info[i];
        printf(
            "> Send pin[pid_id=%d] to the "
            "worker[address=%s:%s | %s:%s]\n",
            buffer.pin.pin_id, info->host, info->port, info->numeric_host, info->numeric_port);
    }

    return true;
}

static bool poll_workers_on_the_first_stage(PinsQueue pins_1_to_2) {
    if (server.first_workers_arr_size == 0 || pins_queue_full(pins_1_to_2)) {
        return true;
    }

    
    if (!server_lock_first_mutex(&server)) {
        return false;
    }
    bool res =
        poll_workers(server.first_workers_fds, server.first_workers_info,
                     &server.first_workers_arr_size, MAX_NUMBER_OF_FIRST_WORKERS, pins_1_to_2);
    if (!server_unlock_first_mutex(&server)) {
        return false;
    }
    // printf("> Ended polling workers on the first stage\n");

    return res;
}

static bool poll_workers_on_the_second_stage(PinsQueue pins_1_to_2, PinsQueue pins_2_to_3) {
    if (server.second_workers_arr_size == 0 ||
        (pins_queue_full(pins_1_to_2) && pins_queue_empty(pins_2_to_3))) {
        return true;
    }

    // printf("> Starting to poll workers on the second stage\n");
    if (!server_lock_second_mutex(&server)) {
        return false;
    }
    bool res = send_pins_to_workers(server.second_workers_fds, server.second_workers_info,
                                    &server.second_workers_arr_size, MAX_NUMBER_OF_SECOND_WORKERS,
                                    pins_1_to_2);
    if (res) {
        poll_workers(server.second_workers_fds, server.second_workers_info,
                     &server.second_workers_arr_size, MAX_NUMBER_OF_SECOND_WORKERS, pins_2_to_3);
    }
    if (!server_unlock_second_mutex(&server)) {
        return false;
    }
    // printf("> Ended polling workers on the second stage\n");

    return res;
}

static bool poll_workers_on_the_third_stage(PinsQueue pins_2_to_3) {
    if (server.third_workers_arr_size == 0 || pins_queue_empty(pins_2_to_3)) {
        return true;
    }

    // printf("> Starting to poll workers on the third stage\n");
    if (!server_lock_third_mutex(&server)) {
        return false;
    }
    bool res = send_pins_to_workers(server.third_workers_fds, server.third_workers_info,
                                    &server.third_workers_arr_size, MAX_NUMBER_OF_THIRD_WORKERS,
                                    pins_2_to_3);
    if (!server_unlock_third_mutex(&server)) {
        return false;
    }
    // printf("> Ended polling workers on the third stage\n");

    return res;
}

static void* workers_poller(void* unused) {
    (void)unused;
    const struct timespec sleep_time = {
        .tv_sec  = 1,
        .tv_nsec = 500000000,
    };

    PinsQueue pins_1_to_2 = {0};
    PinsQueue pins_2_to_3 = {0};
    while (is_poller_running) {
        if (!poll_workers_on_the_first_stage(pins_1_to_2)) {
            fprintf(stderr, "> Could not poll workers on the first stage\n");
            break;
        }
        if (!poll_workers_on_the_second_stage(pins_1_to_2, pins_2_to_3)) {
            fprintf(stderr, "> Could not poll workers on the second stage\n");
            break;
        }
        if (!poll_workers_on_the_third_stage(pins_2_to_3)) {
            fprintf(stderr, "> Could not poll workers on the third stage\n");
            break;
        }
        if (nanosleep(&sleep_time, NULL) == -1) {
            if (errno != EINTR) {  // if not interrupted by the signal
                perror("nanosleep");
            }
            break;
        }
    }

    int32_t ret       = is_poller_running ? EXIT_FAILURE : EXIT_SUCCESS;
    is_poller_running = false;
    return (void*)(uintptr_t)(uint32_t)ret;
}

static int start_runtime_loop() {
    pthread_t poll_thread = 0;
    int ret               = pthread_create(&poll_thread, NULL, &workers_poller, NULL);
    if (ret != 0) {
        errno = ret;
        perror("pthread_create");
        return EXIT_FAILURE;
    }

    printf("> Started polling thread\n> Server ready to accept connections\n");

    while (is_acceptor_running) {
        WorkerType type;
        size_t insert_index;
        server_accept_worker(&server, &type, &insert_index);
    }

    void* poll_ret       = NULL;
    int pthread_join_ret = pthread_join(poll_thread, &poll_ret);
    if (pthread_join_ret != 0) {
        errno = pthread_join_ret;
        perror("pthread_join");
    }

    send_shutdown_signal_to_all(&server);
    printf(
        "> Sent shutdown signals to all clients\n"
        "> Started waiting for %u seconds before closing the sockets...\n",
        (uint32_t)MAX_SLEEP_TIME);
    sleep(MAX_SLEEP_TIME);

    return (int)(uintptr_t)poll_ret;
}

static int run_server(uint16_t server_port) {
    if (!init_server(&server, server_port)) {
        return EXIT_FAILURE;
    }

    int ret = start_runtime_loop(&server);
    deinit_server(&server);
    printf("> Deinitialized server resources\n");
    return ret;
}

int main(int argc, const char* argv[]) {
    setup_signal_handler();

    ParseResultServer res = parse_args_server(argc, argv);
    if (res.status != PARSE_SUCCESS) {
        print_invalid_args_error_server(res.status, argv[0]);
        return EXIT_FAILURE;
    }

    return run_server(res.port);
}

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
#include "client-tools.h"
#include "net-config.h"   
#include "pin.h"           
#include "server-tools.h" 


static struct Server server                      = {0};
static volatile bool is_acceptor_running         = true;
static volatile bool is_poller_running           = true;
static volatile bool is_logger_running           = true;
static volatile bool is_managers_handler_running = true;
static volatile pthread_t app_threads[4]         = {(pthread_t)-1, (pthread_t)-1, (pthread_t)-1,
                                                    (pthread_t)-1};
static volatile size_t app_threads_size          = 0;
static volatile bool disposed                    = false;

static void stop_all_threads() {
    if (disposed) {
        return;
    }
    disposed = true;

    is_acceptor_running         = false;
    is_poller_running           = false;
    is_logger_running           = false;
    is_managers_handler_running = false;
    for (size_t i = 0; i < sizeof(app_threads) / sizeof(app_threads[0]); i++) {
        if (app_threads[i] != (pthread_t)-1) {
            int err_code = pthread_cancel(app_threads[i]);
            if (err_code != 0) {
                errno = err_code;
                perror("stop_all_threads(pthread_cancel)");
            }
        }
    }
}
static void signal_handler(int sig) {
    fprintf(stderr, "> Received signal %d\n", sig);
    stop_all_threads();
}
static void setup_signal_handler() {
    const int handled_signals[] = {
        SIGABRT, SIGINT, SIGTERM, SIGSEGV, SIGQUIT, SIGKILL,
    };
    for (size_t i = 0; i < sizeof(handled_signals) / sizeof(handled_signals[0]); i++) {
        signal(handled_signals[i], signal_handler);
    }
}

static void print_and_enqueue_log(const ServerLog* log) {
    puts(log->message);
    if (!nonblocking_enqueue_log(&server, log)) {
        fputs("> Logs queue if full. Can't add new log to the queue\n\n", stderr);
    }
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
        if (!nonblocking_poll_workers_on_the_first_stage(&server, pins_1_to_2)) {
            fprintf(stderr, "> Could not poll workers on the first stage\n");
            break;
        }
        if (!nonblocking_poll_workers_on_the_second_stage(&server, pins_1_to_2, pins_2_to_3)) {
            fprintf(stderr, "> Could not poll workers on the second stage\n");
            break;
        }
        if (!nonblocking_poll_workers_on_the_third_stage(&server, pins_2_to_3)) {
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

    int32_t ret = is_poller_running ? EXIT_FAILURE : EXIT_SUCCESS;
    if (is_poller_running) {
        stop_all_threads();
    }
    return (void*)(uintptr_t)(uint32_t)ret;
}

static void* logs_sender(void* unused) {
    (void)unused;
    const struct timespec sleep_time = {
        .tv_sec  = 1,
        .tv_nsec = 500000000,
    };

    ServerLog log = {0};
    while (is_logger_running) {
        if (!dequeue_log(&server, &log)) {
            fputs("> Could not get next log\n", stderr);
            break;
        }

        send_server_log(&server, &log);
        if (nanosleep(&sleep_time, NULL) == -1) {
            if (errno != EINTR) {  // if not interrupted by the signal
                perror("nanosleep");
            }
            break;
        }
    }

    int32_t ret = is_logger_running ? EXIT_FAILURE : EXIT_SUCCESS;
    if (is_logger_running) {
        stop_all_threads();
    }
    return (void*)(uintptr_t)(uint32_t)ret;
}

static void* managers_handler(void* unused) {
    (void)unused;
    const struct timespec sleep_time = {
        .tv_sec  = 1,
        .tv_nsec = 500000000,
    };

    ServerLog log;
    ServerCommand cmd = {0};
    while (is_managers_handler_running) {
        if (nanosleep(&sleep_time, NULL) == -1) {
            if (errno != EINTR) {  // if not interrupted by the signal
                perror("nanosleep");
            }
            break;
        }
        size_t manager_index = (size_t)-1;
        if (!nonblocking_poll_managers(&server, &cmd, &manager_index)) {
            fputs("> Could not get next command\n", stderr);
            break;
        }
        if (manager_index == (size_t)-1) {
            continue;
        }
        const ClientMetaInfo* const info = &server.managers_info[manager_index];
        int ret                          = snprintf(log.message, sizeof(log.message),
                                                    "> Received command to shutdown "
                                                                             "client[address=%s:%u]\n"
                                                                             "  from manager[address=%s:%s | %s:%s]\n",
                                                    cmd.ip_address, cmd.port, info->host, info->port, info->numeric_host,
                                                    info->numeric_port);
        assert(ret > 0);
        assert(log.message[0] != '\0');
        print_and_enqueue_log(&log);
        ServerCommandResult res = execute_command(&server, &cmd);
        ret                     = snprintf(log.message, sizeof(log.message),
                                           "> Executed shutdown command on "
                                                               "client at address %s:%u\n"
                                                               "  Server result: %s\n",
                                           cmd.ip_address, cmd.port, server_command_result_to_string(res));
        assert(ret > 0);
        assert(log.message[0] != '\0');
        print_and_enqueue_log(&log);
        if (!send_command_result_to_manager(&server, res, manager_index)) {
            fputs("> Could not send command result to manger\n", stderr);
            break;
        }
        ret = snprintf(log.message, sizeof(log.message),
                       "> Send command result back to "
                       "manager[address=%s:%s | %s:%s]\n",
                       info->host, info->port, info->numeric_host, info->numeric_port);
        assert(ret > 0);
        assert(log.message[0] != '\0');
        print_and_enqueue_log(&log);
    }

    int32_t ret = is_managers_handler_running ? EXIT_FAILURE : EXIT_SUCCESS;
    if (is_managers_handler_running) {
        stop_all_threads();
    }
    return (void*)(uintptr_t)(uint32_t)ret;
}

static bool create_thread(pthread_t* pthread_id, void*(handler)(void*)) {
    int ret = pthread_create(pthread_id, NULL, handler, NULL);
    if (ret != 0) {
        stop_all_threads();
        errno = ret;
        perror("pthread_create");
        return false;
    }
    assert(app_threads_size < (sizeof(app_threads) / sizeof(app_threads[0])));
    app_threads[app_threads_size++] = *pthread_id;
    return true;
}

static int join_thread(pthread_t pthread_id) {
    void* poll_ret       = NULL;
    int pthread_join_ret = pthread_join(pthread_id, &poll_ret);
    if (pthread_join_ret != 0) {
        errno = pthread_join_ret;
        perror("pthread_join");
    }
    return (int)(uintptr_t)poll_ret;
}

static int start_runtime_loop() {
    pthread_t poll_thread;
    if (!create_thread(&poll_thread, &workers_poller)) {
        return EXIT_FAILURE;
    }
    printf("> Started polling thread\n");

    pthread_t logs_thread;
    if (!create_thread(&logs_thread, &logs_sender)) {
        pthread_cancel(poll_thread);
        return EXIT_FAILURE;
    }
    printf("> Started logging thread\n");

    pthread_t managers_thread;
    if (!create_thread(&managers_thread, &managers_handler)) {
        pthread_cancel(logs_thread);
        pthread_cancel(poll_thread);
        return EXIT_FAILURE;
    }
    printf("> Started managers thread\n");

    printf("> Server ready to accept connections\n");
    while (is_acceptor_running) {
        if (!server_accept_client(&server)) {
            break;
        }
    }

    const int ret = is_acceptor_running ? EXIT_FAILURE : EXIT_SUCCESS;
    if (is_acceptor_running) {
        stop_all_threads();
    }
    const int ret_1 = join_thread(logs_thread);
    printf("Joined logging thread\n");
    const int ret_2 = join_thread(poll_thread);
    printf("Joined polling thread\n");
    const int ret_3 = join_thread(managers_thread);
    printf("Joined managers thread\n");

    printf("> Started sending shutdown signals to all clients\n");
    send_shutdown_signal_to_all(&server);
    printf(
        "> Sent shutdown signals to all clients\n"
        "> Started waiting for %u seconds before closing the sockets\n",
        (uint32_t)MAX_SLEEP_TIME);
    sleep(MAX_SLEEP_TIME);

    return ret | ret_1 | ret_2 | ret_3;
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

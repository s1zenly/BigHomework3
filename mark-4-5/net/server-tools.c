#include "server-tools.h"

#include <arpa/inet.h>              
#include <assert.h>                 
#include <bits/socket-constants.h>  
#include <netdb.h>                  
#include <netinet/in.h>            
#include <netinet/tcp.h>            
#include <stdbool.h>               
#include <stdint.h>                 
#include <stdio.h>                
#include <string.h>                 
#include <sys/socket.h>            
#include <unistd.h>                 

#include "../util/config.h"  
#include "net-config.h"      

static bool setup_server(int server_sock_fd, struct sockaddr_in* server_sock_addr,
                         uint16_t server_port) {
    server_sock_addr->sin_family      = AF_INET;
    server_sock_addr->sin_port        = htons(server_port);
    server_sock_addr->sin_addr.s_addr = htonl(INADDR_ANY);

    bool bind_failed = bind(server_sock_fd, (const struct sockaddr*)server_sock_addr,
                            sizeof(*server_sock_addr)) == -1;
    if (bind_failed) {
        perror("bind");
        return false;
    }

    bool listen_failed = listen(server_sock_fd, MAX_CONNECTIONS_PER_SERVER) == -1;
    if (listen_failed) {
        perror("listen");
        return false;
    }

    return true;
}

bool init_server(Server server, uint16_t server_port) {
    memset(server, 0, sizeof(*server));
    if (pthread_mutex_init(&server->first_workers_mutex, NULL) != 0) {
        goto init_server_cleanup_empty;
    }
    if (pthread_mutex_init(&server->second_workers_mutex, NULL) != 0) {
        goto init_server_cleanup_mutex_1;
    }
    if (pthread_mutex_init(&server->third_workers_mutex, NULL) != 0) {
        goto init_server_cleanup_mutex_2;
    }
    server->sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->sock_fd == -1) {
        perror("socket");
        goto init_server_cleanup_mutex_3;
    }
    if (!setup_server(server->sock_fd, &server->sock_addr, server_port)) {
        goto init_server_cleanup_mutex_3_socket;
    }
    memset(server->first_workers_fds, -1, sizeof(int) * MAX_NUMBER_OF_FIRST_WORKERS);
    memset(server->second_workers_fds, -1, sizeof(int) * MAX_NUMBER_OF_SECOND_WORKERS);
    memset(server->third_workers_fds, -1, sizeof(int) * MAX_NUMBER_OF_THIRD_WORKERS);
    return true;

init_server_cleanup_mutex_3_socket:
    close(server->sock_fd);
init_server_cleanup_mutex_3:
    pthread_mutex_destroy(&server->third_workers_mutex);
init_server_cleanup_mutex_2:
    pthread_mutex_destroy(&server->second_workers_mutex);
init_server_cleanup_mutex_1:
    pthread_mutex_destroy(&server->first_workers_mutex);
init_server_cleanup_empty:
    return false;
}

static void close_workers_fds(int fds[], size_t array_max_size) {
    for (size_t i = 0; i < array_max_size; i++) {
        if (fds[i] != -1) {
            close(fds[i]);
        }
    }
}

void deinit_server(Server server) {
    close_workers_fds(server->third_workers_fds, MAX_NUMBER_OF_THIRD_WORKERS);
    close_workers_fds(server->second_workers_fds, MAX_NUMBER_OF_SECOND_WORKERS);
    close_workers_fds(server->first_workers_fds, MAX_NUMBER_OF_FIRST_WORKERS);
    close(server->sock_fd);
    pthread_mutex_destroy(&server->third_workers_mutex);
    pthread_mutex_destroy(&server->second_workers_mutex);
    pthread_mutex_destroy(&server->first_workers_mutex);
}

static bool receive_worker_type(int worker_sock_fd, WorkerType* type) {
    union {
        char bytes[sizeof(*type)];
        WorkerType type;
    } buffer = {0};

    bool received_type;
    int tries = 4;
    do {
        received_type = recv(worker_sock_fd, buffer.bytes, sizeof(buffer.bytes), MSG_DONTWAIT) ==
                        sizeof(buffer.bytes);
    } while (!received_type && --tries > 0);

    if (!received_type) {
        return false;
    }

    *type = buffer.type;
    return true;
}

static void fill_worker_metainfo(WorkerMetainfo* info, const struct sockaddr_in* worker_addr) {
    int gai_err = getnameinfo((const struct sockaddr*)worker_addr, sizeof(*worker_addr), info->host,
                              sizeof(info->host), info->port, sizeof(info->port), 0);
    if (gai_err != 0) {
        fprintf(stderr, "> Could not fetch info about socket address: %s\n", gai_strerror(gai_err));
        strcpy(info->host, "unknown host");
        strcpy(info->port, "unknown port");
    }

    gai_err = getnameinfo((const struct sockaddr*)worker_addr, sizeof(*worker_addr),
                          info->numeric_host, sizeof(info->numeric_host), info->numeric_port,
                          sizeof(info->numeric_port), NI_NUMERICSERV | NI_NUMERICHOST);
    if (gai_err != 0) {
        fprintf(stderr, "> Could not fetch info about socket address: %s\n", gai_strerror(gai_err));
        strcpy(info->numeric_host, "unknown host");
        strcpy(info->numeric_port, "unknown port");
    }
}

static size_t insert_new_worker_into_arrays(struct sockaddr_in workers_addrs[], int workers_fds[],
                                            volatile size_t* array_size,
                                            const size_t max_array_size,
                                            WorkerMetainfo workers_info[],
                                            const struct sockaddr_in* worker_addr,
                                            int worker_sock_fd) {
    assert(*array_size <= max_array_size);
    for (size_t i = 0; i < max_array_size; i++) {
        if (workers_fds[i] != -1) {
            continue;
        }

        assert(array_size);
        assert(*array_size < max_array_size);
        assert(worker_addr);
        assert(workers_addrs);
        workers_addrs[i] = *worker_addr;
        assert(array_size);
        (*array_size)++;
        fill_worker_metainfo(&workers_info[i], worker_addr);
        assert(workers_fds);
        workers_fds[i] = worker_sock_fd;
        return i;
    }

    return (size_t)-1;
}

static bool handle_new_worker(Server server, const struct sockaddr_storage* storage,
                              socklen_t worker_addrlen, int worker_sock_fd, WorkerType* type,
                              size_t* insert_index) {
    if (worker_addrlen != sizeof(struct sockaddr_in)) {
        fprintf(stderr, "> Unknown worker of size %u\n", worker_addrlen);
        return false;
    }


    uint32_t val = 1;
    if (setsockopt(worker_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
        perror("setsockopt");
        return false;
    }


    val = MAX_SLEEP_TIME;
    if (setsockopt(worker_sock_fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) == -1) {
        perror("setsockopt");
        return false;
    }

    struct sockaddr_in worker_addr = {0};
    memcpy(&worker_addr, storage, sizeof(worker_addr));

    if (!receive_worker_type(worker_sock_fd, type)) {
        fprintf(stderr, "> Could not get worker type at port %u\n", worker_addr.sin_port);
        return false;
    }

    const WorkerMetainfo* info_array;
    switch (*type) {
        case FIRST_STAGE_WORKER: {
            if (!server_lock_first_mutex(server)) {
                return false;
            }
            *insert_index = insert_new_worker_into_arrays(
                server->first_workers_addrs, server->first_workers_fds,
                &server->first_workers_arr_size, MAX_NUMBER_OF_FIRST_WORKERS,
                server->first_workers_info, &worker_addr, worker_sock_fd);
            if (!server_unlock_first_mutex(server)) {
                return false;
            }
            info_array = server->first_workers_info;
        } break;
        case SECOND_STAGE_WORKER: {
            if (!server_lock_second_mutex(server)) {
                return false;
            }
            *insert_index = insert_new_worker_into_arrays(
                server->second_workers_addrs, server->second_workers_fds,
                &server->second_workers_arr_size, MAX_NUMBER_OF_SECOND_WORKERS,
                server->second_workers_info, &worker_addr, worker_sock_fd);
            if (!server_unlock_second_mutex(server)) {
                return false;
            }
            info_array = server->second_workers_info;
        } break;
        case THIRD_STAGE_WORKER: {
            if (!server_lock_third_mutex(server)) {
                return false;
            }
            *insert_index = insert_new_worker_into_arrays(
                server->third_workers_addrs, server->third_workers_fds,
                &server->third_workers_arr_size, MAX_NUMBER_OF_THIRD_WORKERS,
                server->third_workers_info, &worker_addr, worker_sock_fd);
            if (!server_unlock_third_mutex(server)) {
                return false;
            }
            info_array = server->third_workers_info;
        } break;
        default:
            fprintf(stderr, "> Unknown worker type: %d\n", *type);
            return false;
    }

    const char* worker_type = worker_type_to_string(*type);
    if (*insert_index == (size_t)-1) {
        fprintf(stderr,
                "> Can't accept new worker: limit for workers "
                "of type \"%s\" has been reached\n",
                worker_type);
        return false;
    }

    const WorkerMetainfo* info = &info_array[*insert_index];
    printf("> Accepted new worker with type \"%s\"(address=%s:%s | %s:%s)\n", worker_type,
           info->host, info->port, info->numeric_host, info->numeric_port);
    return true;
}

static void send_shutdown_signal_to_one_impl(int sock_fd, const WorkerMetainfo* info);

bool server_accept_worker(Server server, WorkerType* type, size_t* insert_index) {
    struct sockaddr_storage storage;
    socklen_t worker_addrlen = sizeof(storage);
    int worker_sock_fd       = accept(server->sock_fd, (struct sockaddr*)&storage, &worker_addrlen);
    if (worker_sock_fd == -1) {
        if (errno != EINTR) {
            perror("accept");
        }
        return false;
    }
    if (!handle_new_worker(server, &storage, worker_addrlen, worker_sock_fd, type, insert_index)) {
        send_shutdown_signal_to_one_impl(worker_sock_fd, NULL);
        close(worker_sock_fd);
        return false;
    }
    return true;
}

static void send_shutdown_signal_to_one_impl(int sock_fd, const WorkerMetainfo* info) {
    const char* host         = info ? info->host : "unknown host";
    const char* port         = info ? info->port : "unknown port";
    const char* numeric_host = info ? info->numeric_host : "unknown host";
    const char* numeric_port = info ? info->numeric_port : "unknown port";
    if (send(sock_fd, SHUTDOWN_MESSAGE, SHUTDOWN_MESSAGE_SIZE, 0) != SHUTDOWN_MESSAGE_SIZE) {
        perror("send");
        fprintf(stderr,
                "> Could not send shutdown signal to the "
                "worker[address=%s:%s | %s:%s]\n",
                host, port, numeric_host, numeric_port);
    } else {
        printf(
            "> Sent shutdown signal to the "
            "worker[address=%s:%s | %s:%s]\n",
            host, port, numeric_host, numeric_port);
        if (shutdown(sock_fd, SHUT_RDWR) == -1) {
            perror("shutdown");
        }
    }
}

void send_shutdown_signal_to_one(const Server server, WorkerType type, size_t index) {
    const int* fds_arr;
    const WorkerMetainfo* infos_arr;
    switch (type) {
        case FIRST_STAGE_WORKER:
            assert(index < MAX_NUMBER_OF_FIRST_WORKERS);
            fds_arr   = server->first_workers_fds;
            infos_arr = server->first_workers_info;
            break;
        case SECOND_STAGE_WORKER:
            assert(index < MAX_NUMBER_OF_SECOND_WORKERS);
            fds_arr   = server->second_workers_fds;
            infos_arr = server->second_workers_info;
            break;
        case THIRD_STAGE_WORKER:
            assert(index < MAX_NUMBER_OF_THIRD_WORKERS);
            fds_arr   = server->third_workers_fds;
            infos_arr = server->third_workers_info;
            break;
        default:
            return;
    }

    int sock_fd = fds_arr[index];
    if (sock_fd == -1) {
        return;
    }

    send_shutdown_signal_to_one_impl(sock_fd, &infos_arr[index]);
}

static void send_shutdown_signal_to_all_in_arr_impl(const int sock_fds[],
                                                    const WorkerMetainfo workers_info[],
                                                    size_t max_array_size) {
    for (size_t i = 0; i < max_array_size; i++) {
        int sock_fd = sock_fds[i];
        if (sock_fd != -1) {
            send_shutdown_signal_to_one_impl(sock_fd, &workers_info[i]);
        }
    }
}

void send_shutdown_signal_to_first_workers(Server server) {
    if (server_lock_first_mutex(server)) {
        send_shutdown_signal_to_all_in_arr_impl(
            server->first_workers_fds, server->first_workers_info, MAX_NUMBER_OF_FIRST_WORKERS);
        server->first_workers_arr_size = 0;
        server_unlock_first_mutex(server);
    }
}

void send_shutdown_signal_to_second_workers(Server server) {
    if (server_lock_second_mutex(server)) {
        send_shutdown_signal_to_all_in_arr_impl(
            server->second_workers_fds, server->second_workers_info, MAX_NUMBER_OF_SECOND_WORKERS);
        server->second_workers_arr_size = 0;
        server_unlock_second_mutex(server);
    }
}

void send_shutdown_signal_to_third_workers(Server server) {
    if (server_lock_third_mutex(server)) {
        send_shutdown_signal_to_all_in_arr_impl(
            server->third_workers_fds, server->third_workers_info, MAX_NUMBER_OF_THIRD_WORKERS);
        server->third_workers_arr_size = 0;
        server_unlock_third_mutex(server);
    }
}

void send_shutdown_signal_to_all_of_type(Server server, WorkerType type) {
    switch (type) {
        case FIRST_STAGE_WORKER:
            send_shutdown_signal_to_first_workers(server);
            break;
        case SECOND_STAGE_WORKER:
            send_shutdown_signal_to_second_workers(server);
            break;
        case THIRD_STAGE_WORKER:
            send_shutdown_signal_to_third_workers(server);
            break;
        default:
            return;
    }
}

void send_shutdown_signal_to_all(Server server) {
    send_shutdown_signal_to_first_workers(server);
    send_shutdown_signal_to_second_workers(server);
    send_shutdown_signal_to_third_workers(server);
}

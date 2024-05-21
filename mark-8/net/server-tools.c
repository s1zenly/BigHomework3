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

    if (setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &(int){true}, sizeof(int)) == -1) {
        perror("setsockopt");
        return false;
    }

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
    int err_code            = 0;
    const char* error_cause = "";
    memset(server, 0, sizeof(*server));

    err_code = pthread_mutex_init(&server->first_workers_mutex, NULL);
    if (err_code != 0) {
        error_cause = "pthread_mutex_init";
        goto init_server_cleanup_empty;
    }
    err_code = pthread_mutex_init(&server->second_workers_mutex, NULL);
    if (err_code != 0) {
        error_cause = "pthread_mutex_init";
        goto init_server_cleanup_mutex_1;
    }
    err_code = pthread_mutex_init(&server->third_workers_mutex, NULL);
    if (err_code != 0) {
        error_cause = "pthread_mutex_init";
        goto init_server_cleanup_mutex_2;
    }
    err_code = pthread_mutex_init(&server->logs_collectors_mutex, NULL);
    if (err_code != 0) {
        error_cause = "pthread_mutex_init";
        goto init_server_cleanup_mutex_3;
    }
    server->sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->sock_fd == -1) {
        error_cause = "socket";
        err_code    = errno;
        goto init_server_cleanup_mutex_4;
    }
    if (!init_server_logs_queue(&server->logs_queue)) {
        error_cause = "init_server_logs_queue";
        goto init_server_cleanup_mutex_4_socket;
    }
    if (!setup_server(server->sock_fd, &server->sock_addr, server_port)) {
        error_cause = "setup_server";
        goto init_server_cleanup_mutex_4_socket_logs_queue;
    }
    memset(server->first_workers_fds, -1, sizeof(int) * MAX_NUMBER_OF_FIRST_WORKERS);
    memset(server->second_workers_fds, -1, sizeof(int) * MAX_NUMBER_OF_SECOND_WORKERS);
    memset(server->third_workers_fds, -1, sizeof(int) * MAX_NUMBER_OF_THIRD_WORKERS);
    memset(server->logs_collectors_fds, -1, sizeof(int) * MAX_NUMBER_OF_LOGS_COLLECTORS);
    return true;

init_server_cleanup_mutex_4_socket_logs_queue:
    deinit_server_logs_queue(&server->logs_queue);
init_server_cleanup_mutex_4_socket:
    close(server->sock_fd);
init_server_cleanup_mutex_4:
    pthread_mutex_destroy(&server->logs_collectors_mutex);
init_server_cleanup_mutex_3:
    pthread_mutex_destroy(&server->third_workers_mutex);
init_server_cleanup_mutex_2:
    pthread_mutex_destroy(&server->second_workers_mutex);
init_server_cleanup_mutex_1:
    pthread_mutex_destroy(&server->first_workers_mutex);
init_server_cleanup_empty:
    if (err_code != 0) {
        errno = err_code;
        perror(error_cause);
    }
    return false;
}

static void close_fds(int fds[], size_t array_max_size) {
    for (size_t i = 0; i < array_max_size; i++) {
        if (fds[i] != -1) {
            close(fds[i]);
        }
    }
}

void deinit_server(Server server) {
    deinit_server_logs_queue(&server->logs_queue);
    close_fds(server->logs_collectors_fds, MAX_NUMBER_OF_LOGS_COLLECTORS);
    close_fds(server->third_workers_fds, MAX_NUMBER_OF_THIRD_WORKERS);
    close_fds(server->second_workers_fds, MAX_NUMBER_OF_SECOND_WORKERS);
    close_fds(server->first_workers_fds, MAX_NUMBER_OF_FIRST_WORKERS);
    close(server->sock_fd);
    pthread_mutex_destroy(&server->logs_collectors_mutex);
    pthread_mutex_destroy(&server->third_workers_mutex);
    pthread_mutex_destroy(&server->second_workers_mutex);
    pthread_mutex_destroy(&server->first_workers_mutex);
}

static bool receive_client_type(int client_sock_fd, ClientType* type) {
    union {
        char bytes[NET_BUFFER_SIZE];
        ClientType type;
    } buffer = {0};

    bool received_type;
    int tries = 8;
    do {
        received_type =
            recv(client_sock_fd, buffer.bytes, sizeof(*type), MSG_DONTWAIT) == sizeof(*type);
    } while (!received_type && --tries > 0);

    if (!received_type) {
        return false;
    }

    *type = buffer.type;
    return true;
}

static void fill_client_metainfo(ClientMetaInfo* info, const struct sockaddr_in* client_addr) {
    int gai_err = getnameinfo((const struct sockaddr*)client_addr, sizeof(*client_addr), info->host,
                              sizeof(info->host), info->port, sizeof(info->port), 0);
    if (gai_err != 0) {
        fprintf(stderr, "> Could not fetch info about socket address: %s\n", gai_strerror(gai_err));
        strcpy(info->host, "unknown host");
        strcpy(info->port, "unknown port");
    }

    gai_err = getnameinfo((const struct sockaddr*)client_addr, sizeof(*client_addr),
                          info->numeric_host, sizeof(info->numeric_host), info->numeric_port,
                          sizeof(info->numeric_port), NI_NUMERICSERV | NI_NUMERICHOST);
    if (gai_err != 0) {
        fprintf(stderr, "> Could not fetch info about socket address: %s\n", gai_strerror(gai_err));
        strcpy(info->numeric_host, "unknown host");
        strcpy(info->numeric_port, "unknown port");
    }
}

static size_t insert_new_client_into_arrays(struct sockaddr_in clients_addrs[], int clients_fds[],
                                            volatile size_t* array_size,
                                            const size_t max_array_size,
                                            ClientMetaInfo clients_info[],
                                            const struct sockaddr_in* client_addr,
                                            int client_sock_fd) {
    assert(*array_size <= max_array_size);
    for (size_t i = 0; i < max_array_size; i++) {
        if (clients_fds[i] != -1) {
            continue;
        }

        assert(array_size);
        assert(*array_size < max_array_size);
        assert(client_addr);
        assert(clients_addrs);
        clients_addrs[i] = *client_addr;
        assert(array_size);
        (*array_size)++;
        fill_client_metainfo(&clients_info[i], client_addr);
        assert(clients_fds);
        clients_fds[i] = client_sock_fd;
        return i;
    }

    return (size_t)-1;
}

static bool handle_new_client(Server server, const struct sockaddr_storage* storage,
                              socklen_t client_addrlen, int client_sock_fd, ClientType* type,
                              size_t* insert_index) {
    if (client_addrlen != sizeof(struct sockaddr_in)) {
        fprintf(stderr, "> Unknown client of size %u\n", client_addrlen);
        return false;
    }

    
    uint32_t val = 1;
    if (setsockopt(client_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
        perror("setsockopt");
        return false;
    }

    
    val = MAX_SLEEP_TIME;
    if (setsockopt(client_sock_fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) == -1) {
        perror("setsockopt");
        return false;
    }

    struct sockaddr_in client_addr = {0};
    memcpy(&client_addr, storage, sizeof(client_addr));

    if (!receive_client_type(client_sock_fd, type)) {
        fprintf(stderr, "> Could not get client type at port %u\n", client_addr.sin_port);
        return false;
    }

    const ClientMetaInfo* info_array;
    switch (*type) {
        case FIRST_STAGE_WORKER_CLIENT: {
            if (!server_lock_first_mutex(server)) {
                return false;
            }
            *insert_index = insert_new_client_into_arrays(
                server->first_workers_addrs, server->first_workers_fds,
                &server->first_workers_arr_size, MAX_NUMBER_OF_FIRST_WORKERS,
                server->first_workers_info, &client_addr, client_sock_fd);
            if (!server_unlock_first_mutex(server)) {
                return false;
            }
            info_array = server->first_workers_info;
        } break;
        case SECOND_STAGE_WORKER_CLIENT: {
            if (!server_lock_second_mutex(server)) {
                return false;
            }
            *insert_index = insert_new_client_into_arrays(
                server->second_workers_addrs, server->second_workers_fds,
                &server->second_workers_arr_size, MAX_NUMBER_OF_SECOND_WORKERS,
                server->second_workers_info, &client_addr, client_sock_fd);
            if (!server_unlock_second_mutex(server)) {
                return false;
            }
            info_array = server->second_workers_info;
        } break;
        case THIRD_STAGE_WORKER_CLIENT: {
            if (!server_lock_third_mutex(server)) {
                return false;
            }
            *insert_index = insert_new_client_into_arrays(
                server->third_workers_addrs, server->third_workers_fds,
                &server->third_workers_arr_size, MAX_NUMBER_OF_THIRD_WORKERS,
                server->third_workers_info, &client_addr, client_sock_fd);
            if (!server_unlock_third_mutex(server)) {
                return false;
            }
            info_array = server->third_workers_info;
        } break;
        case LOGS_COLLECTOR_CLIENT: {
            if (!server_lock_logs_collectors_mutex(server)) {
                return false;
            }
            *insert_index = insert_new_client_into_arrays(
                server->logs_collectors_addrs, server->logs_collectors_fds,
                &server->logs_collectors_arr_size, MAX_NUMBER_OF_LOGS_COLLECTORS,
                server->logs_collectors_info, &client_addr, client_sock_fd);
            if (!server_unlock_logs_collectors_mutex(server)) {
                return false;
            }
            info_array = server->logs_collectors_info;
        } break;
        default:
            fprintf(stderr, "> Unknown client type: %d\n", *type);
            return false;
    }

    const char* client_type_str = client_type_to_string(*type);
    if (*insert_index == (size_t)-1) {
        fprintf(stderr,
                "> Can't accept new client: limit for client "
                "of type \"%s\" has been reached\n",
                client_type_str);
        return false;
    }

    const ClientMetaInfo* info = &info_array[*insert_index];
    printf("> Accepted new client with type \"%s\"[address=%s:%s | %s:%s]\n\n", client_type_str,
           info->host, info->port, info->numeric_host, info->numeric_port);
    return true;
}

static void send_shutdown_signal_to_one_impl(int sock_fd, const ClientMetaInfo* info);

bool server_accept_client(Server server, ClientType* type, size_t* insert_index) {
    struct sockaddr_storage storage;
    socklen_t client_addrlen = sizeof(storage);
    int client_sock_fd       = accept(server->sock_fd, (struct sockaddr*)&storage, &client_addrlen);
    if (client_sock_fd == -1) {
        if (errno != EINTR) {
            perror("accept");
        }
        return false;
    }
    if (!handle_new_client(server, &storage, client_addrlen, client_sock_fd, type, insert_index)) {
        send_shutdown_signal_to_one_impl(client_sock_fd, NULL);
        close(client_sock_fd);
        return false;
    }
    return true;
}

bool nonblocking_enqueue_log(Server server, const ServerLog* log) {
    return server_logs_queue_nonblocking_enqueue(&server->logs_queue, log);
}

bool dequeue_log(Server server, ServerLog* log) {
    return server_logs_queue_dequeue(&server->logs_queue, log);
}

static void send_shutdown_signal_to_one_impl(int sock_fd, const ClientMetaInfo* info) {
    const char* host         = info ? info->host : "unknown host";
    const char* port         = info ? info->port : "unknown port";
    const char* numeric_host = info ? info->numeric_host : "unknown host";
    const char* numeric_port = info ? info->numeric_port : "unknown port";
    if (send(sock_fd, SHUTDOWN_MESSAGE, SHUTDOWN_MESSAGE_SIZE, MSG_NOSIGNAL) !=
        SHUTDOWN_MESSAGE_SIZE) {
        perror("send");
        fprintf(stderr,
                "> Could not send shutdown signal to the "
                "client[address=%s:%s | %s:%s]\n",
                host, port, numeric_host, numeric_port);
    } else {
        printf(
            "> Sent shutdown signal to the "
            "client[address=%s:%s | %s:%s]\n",
            host, port, numeric_host, numeric_port);
        if (shutdown(sock_fd, SHUT_RDWR) == -1) {
            perror("shutdown");
        }
    }
}

void send_shutdown_signal_to_one_client(const Server server, ClientType type, size_t index) {
    const int* fds_arr;
    const ClientMetaInfo* infos_arr;
    switch (type) {
        case FIRST_STAGE_WORKER_CLIENT:
            assert(index < MAX_NUMBER_OF_FIRST_WORKERS);
            fds_arr   = server->first_workers_fds;
            infos_arr = server->first_workers_info;
            break;
        case SECOND_STAGE_WORKER_CLIENT:
            assert(index < MAX_NUMBER_OF_SECOND_WORKERS);
            fds_arr   = server->second_workers_fds;
            infos_arr = server->second_workers_info;
            break;
        case THIRD_STAGE_WORKER_CLIENT:
            assert(index < MAX_NUMBER_OF_THIRD_WORKERS);
            fds_arr   = server->third_workers_fds;
            infos_arr = server->third_workers_info;
            break;
        case LOGS_COLLECTOR_CLIENT:
            assert(index < MAX_NUMBER_OF_LOGS_COLLECTORS);
            fds_arr   = server->logs_collectors_fds;
            infos_arr = server->logs_collectors_info;
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
                                                    const ClientMetaInfo clients_info[],
                                                    size_t max_array_size) {
    for (size_t i = 0; i < max_array_size; i++) {
        int sock_fd = sock_fds[i];
        if (sock_fd != -1) {
            send_shutdown_signal_to_one_impl(sock_fd, &clients_info[i]);
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

void send_shutdown_signal_to_logs_collectors(Server server) {
    if (server_lock_logs_collectors_mutex(server)) {
        send_shutdown_signal_to_all_in_arr_impl(server->logs_collectors_fds,
                                                server->logs_collectors_info,
                                                MAX_NUMBER_OF_LOGS_COLLECTORS);
        server->logs_collectors_arr_size = 0;
        server_unlock_logs_collectors_mutex(server);
    }
}

void send_shutdown_signal_to_all_clients_of_type(Server server, ClientType type) {
    switch (type) {
        case FIRST_STAGE_WORKER_CLIENT:
            send_shutdown_signal_to_first_workers(server);
            break;
        case SECOND_STAGE_WORKER_CLIENT:
            send_shutdown_signal_to_second_workers(server);
            break;
        case THIRD_STAGE_WORKER_CLIENT:
            send_shutdown_signal_to_third_workers(server);
            break;
        case LOGS_COLLECTOR_CLIENT:
            send_shutdown_signal_to_logs_collectors(server);
            break;
        default:
            return;
    }
}

void send_shutdown_signal_to_all(Server server) {
    send_shutdown_signal_to_first_workers(server);
    send_shutdown_signal_to_second_workers(server);
    send_shutdown_signal_to_third_workers(server);
    send_shutdown_signal_to_logs_collectors(server);
    if (shutdown(server->sock_fd, SHUT_RDWR) == -1) {
        perror("shutdown[send_shutdown_signal_to_all]");
    }
}

typedef enum HandleResult {
    EMPTY_CLIENT_SOCKET,
    DEAD_CLIENT_SOCKET,
    UNKNOWN_SOCKET_ERROR,
} HandleResult;

static HandleResult handle_socket_op_error(const char* cause) {
    switch (errno) {
        case EAGAIN:
            return EMPTY_CLIENT_SOCKET;
        case EPIPE:
        case ENETDOWN:
        case ENETUNREACH:
        case ENETRESET:
        case ECONNABORTED:
        case ECONNRESET:
        case ECONNREFUSED:
        case EHOSTDOWN:
        case EHOSTUNREACH:
            return DEAD_CLIENT_SOCKET;
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

static bool poll_workers(Server server, int workers_fds[], const ClientMetaInfo workers_info[],
                         volatile size_t* current_workers_online, size_t max_number_of_workers,
                         PinsQueue pins_q) {
    ServerLog log;
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
            switch (handle_socket_op_error("recv")) {
                case EMPTY_CLIENT_SOCKET:
                    continue;
                case DEAD_CLIENT_SOCKET:
                    workers_fds[i] = -1;
                    close(worker_fd);
                    (*current_workers_online)--;
                    continue;
                case UNKNOWN_SOCKET_ERROR:
                default:
                    return false;
            }
        }

        const ClientMetaInfo* info = &workers_info[i];
        snprintf(log.message, sizeof(log.message),
                 "> Received pin[pin_id=%d] from the "
                 "worker[address=%s:%s | %s:%s]\n",
                 buffer.pin.pin_id, info->host, info->port, info->numeric_host, info->numeric_port);
        puts(log.message);
        if (!nonblocking_enqueue_log(server, &log)) {
            fprintf(stderr, "> Logs queue if full. Can't add new log to the queue\n");
        }
        bool res = pins_queue_try_put(pins_q, buffer.pin);
        assert(res);
    }

    return true;
}

static bool send_pins_to_workers(Server server, int workers_fds[],
                                 const ClientMetaInfo workers_info[],
                                 volatile size_t* current_workers_online,
                                 size_t max_number_of_workers, PinsQueue pins_q) {
    ServerLog log;
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

        const ssize_t sent_size =
            send(worker_fd, buffer.bytes, sizeof(Pin), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent_size != sizeof(Pin)) {
            switch (handle_socket_op_error("send")) {
                case EMPTY_CLIENT_SOCKET:
                    continue;
                case DEAD_CLIENT_SOCKET:
                    workers_fds[i] = -1;
                    close(worker_fd);
                    (*current_workers_online)--;
                    continue;
                case UNKNOWN_SOCKET_ERROR:
                default:
                    return false;
            }
        }

        const ClientMetaInfo* info = &workers_info[i];
        snprintf(log.message, sizeof(log.message),
                 "> Send pin[pid_id=%d] to the "
                 "worker[address=%s:%s | %s:%s]\n",
                 buffer.pin.pin_id, info->host, info->port, info->numeric_host, info->numeric_port);
        puts(log.message);
        if (!nonblocking_enqueue_log(server, &log)) {
            fprintf(stderr, "> Logs queue if full. Can't add new log to the queue\n");
        }
    }

    return true;
}

bool nonblocking_poll_workers_on_the_first_stage(Server server, PinsQueue pins_1_to_2) {
    if (server->first_workers_arr_size == 0 || pins_queue_full(pins_1_to_2)) {
        return true;
    }

    if (!server_lock_first_mutex(server)) {
        return false;
    }
    bool res =
        poll_workers(server, server->first_workers_fds, server->first_workers_info,
                     &server->first_workers_arr_size, MAX_NUMBER_OF_FIRST_WORKERS, pins_1_to_2);
    if (!server_unlock_first_mutex(server)) {
        return false;
    }

    return res;
}

bool nonblocking_poll_workers_on_the_second_stage(Server server, PinsQueue pins_1_to_2,
                                                  PinsQueue pins_2_to_3) {
    if (server->second_workers_arr_size == 0 ||
        (pins_queue_full(pins_1_to_2) && pins_queue_empty(pins_2_to_3))) {
        return true;
    }

    if (!server_lock_second_mutex(server)) {
        return false;
    }
    bool res = send_pins_to_workers(server, server->second_workers_fds, server->second_workers_info,
                                    &server->second_workers_arr_size, MAX_NUMBER_OF_SECOND_WORKERS,
                                    pins_1_to_2);
    if (res) {
        poll_workers(server, server->second_workers_fds, server->second_workers_info,
                     &server->second_workers_arr_size, MAX_NUMBER_OF_SECOND_WORKERS, pins_2_to_3);
    }
    if (!server_unlock_second_mutex(server)) {
        return false;
    }

    return res;
}

bool nonblocking_poll_workers_on_the_third_stage(Server server, PinsQueue pins_2_to_3) {
    if (server->third_workers_arr_size == 0 || pins_queue_empty(pins_2_to_3)) {
        return true;
    }

    if (!server_lock_third_mutex(server)) {
        return false;
    }
    bool res = send_pins_to_workers(server, server->third_workers_fds, server->third_workers_info,
                                    &server->third_workers_arr_size, MAX_NUMBER_OF_THIRD_WORKERS,
                                    pins_2_to_3);
    if (!server_unlock_third_mutex(server)) {
        return false;
    }

    return res;
}

void send_server_log(Server server, const ServerLog* log) {
    for (size_t i = 0; i < MAX_NUMBER_OF_LOGS_COLLECTORS; i++) {
        int logs_coll_fd = server->logs_collectors_fds[i];
        if (logs_coll_fd == -1) {
            continue;
        }

        if (send(logs_coll_fd, log->message, sizeof(log->message), MSG_NOSIGNAL) !=
            sizeof(log->message)) {
            const ClientMetaInfo* info = &server->logs_collectors_info[i];
            fprintf(stderr,
                    "> Could not send server log to the "
                    "logs collector[address=%s:%s | %s:%s]\n",
                    info->host, info->port, info->numeric_host, info->port);
            if (handle_socket_op_error("send[send_server_log]") == DEAD_CLIENT_SOCKET) {
                server->logs_collectors_fds[i] = -1;
                fprintf(stderr, "> Logs collector [address=%s:%s | %s:%s] closed connection\n",
                        info->host, info->port, info->numeric_host, info->port);
            }
        }
    }
}

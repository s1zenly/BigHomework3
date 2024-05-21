#include "client-tools.h"

#include <arpa/inet.h>    
#include <errno.h>        
#include <netdb.h>        
#include <netinet/in.h>   
#include <netinet/tcp.h>  
#include <stdint.h>       
#include <stdio.h>        
#include <sys/socket.h>  
#include <unistd.h>       

#include "../util/config.h"  
#include "client-tools.h"
#include "net-config.h" 

static bool send_client_type_info(int socket_fd, struct sockaddr_in* server_sock_addr,
                                  ClientType type) {
    union {
        char bytes[NET_BUFFER_SIZE];
        ClientType type;
    } buffer;
    buffer.type = type;
    if (sendto(socket_fd, buffer.bytes, sizeof(type), MSG_NOSIGNAL,
               (const struct sockaddr*)server_sock_addr,
               sizeof(*server_sock_addr)) != sizeof(type)) {
        perror("sendto[while sending client type to the server]");
        return false;
    }

    printf("Sent type \"%s\" of this client to the server\n", client_type_to_string(type));
    return true;
}

static bool connect_to_server(int socket_fd, struct sockaddr_in* server_sock_addr,
                              const char* server_ip, uint16_t server_port, ClientType type) {
    server_sock_addr->sin_family      = AF_INET;
    server_sock_addr->sin_port        = htons(server_port);
    server_sock_addr->sin_addr.s_addr = inet_addr(server_ip);

    bool failed = connect(socket_fd, (const struct sockaddr*)server_sock_addr,
                          sizeof(*server_sock_addr)) == -1;
    if (failed) {
        perror("connect");
        return false;
    }

    
    uint32_t val = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
        perror("setsockopt");
        return false;
    }


    val = MAX_SLEEP_TIME;
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) == -1) {
        perror("setsockopt");
        return false;
    }

    if (!send_client_type_info(socket_fd, server_sock_addr, type)) {
        fprintf(stderr, "Could not send self type %d to the server %s:%u\n", type, server_ip,
                server_port);
        return false;
    }

    return true;
}

static int create_socket() {
    int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd == -1) {
        perror("socket");
    }
    return sock_fd;
}

bool init_client(Client client, const char* server_ip, uint16_t server_port, ClientType type) {
    client->type           = type;
    client->client_sock_fd = create_socket();
    if (client->client_sock_fd == -1) {
        return false;
    }

    bool connected = connect_to_server(client->client_sock_fd, &client->server_sock_addr, server_ip,
                                       server_port, type);
    if (!connected) {
        close(client->client_sock_fd);
    }

    return connected;
}

void deinit_client(Client client) {
    close(client->client_sock_fd);
}

void print_sock_addr_info(const struct sockaddr* socket_address,
                          const socklen_t socket_address_size) {
    char host_name[1024] = {0};
    char port_str[16]    = {0};
    int gai_err = getnameinfo(socket_address, socket_address_size, host_name, sizeof(host_name),
                              port_str, sizeof(port_str), NI_NUMERICHOST | NI_NUMERICSERV);
    if (gai_err == 0) {
        printf(
            "Numeric socket address: %s\n"
            "Numeric socket port: %s\n",
            host_name, port_str);
    } else {
        fprintf(stderr, "Could not fetch info about socket address: %s\n", gai_strerror(gai_err));
    }

    gai_err = getnameinfo(socket_address, socket_address_size, host_name, sizeof(host_name),
                          port_str, sizeof(port_str), 0);
    if (gai_err == 0) {
        printf("Socket address: %s\nSocket port: %s\n", host_name, port_str);
    }
}

static bool client_handle_errno(const char* cause) {
    uint32_t errno_val = (uint32_t)(errno);
    switch (errno_val) {
        case EAGAIN:
        case ENETDOWN:
        case EPIPE:
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
            return true;
        default:
            perror(cause);
            return false;
    }
}

bool client_should_stop(const Client client) {
    char buffer[NET_BUFFER_SIZE] = {0};
    ssize_t bytes_read           = recv(client->client_sock_fd, buffer, sizeof(buffer),
                                        MSG_DONTWAIT | MSG_PEEK | MSG_NOSIGNAL);
    if (bytes_read < 0) {
        const int errno_val = errno;
        if (errno_val == EAGAIN || errno_val == EWOULDBLOCK) {
            return false;
        }
        client_handle_errno("client_should_stop");
        return true;
    }

    return is_shutdown_message(buffer, (size_t)bytes_read);
}

static void handle_recv_error(const char* bytes, ssize_t read_bytes) {
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

    client_handle_errno("recv");
}

Pin receive_new_pin() {
    Pin pin = {.pin_id = rand()};
    return pin;
}
bool check_pin_crookness(Pin pin) {
    uint32_t sleep_time = (uint32_t)rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME) + MIN_SLEEP_TIME;
    sleep(sleep_time);

    uint32_t x = (uint32_t)pin.pin_id;
#if defined(__GNUC__)
    return __builtin_parity(x) & 1;
#else
    return x & 1;
#endif
}
bool send_pin(int sock_fd, const struct sockaddr_in* sock_addr, Pin pin) {
    bool success = sendto(sock_fd, &pin, sizeof(pin), MSG_NOSIGNAL,
                          (const struct sockaddr*)sock_addr, sizeof(*sock_addr)) == sizeof(pin);
    if (!success) {
        client_handle_errno("sendto");
    }
    return success;
}
bool send_not_croocked_pin(Client worker, Pin pin) {
    assert(is_worker(worker));
    return send_pin(worker->client_sock_fd, &worker->server_sock_addr, pin);
}

static bool receive_data(int sock_fd, void* buffer, size_t buffer_size) {
    ssize_t read_bytes;
    do {
        read_bytes = recv(sock_fd, buffer, buffer_size, MSG_NOSIGNAL);
        if ((size_t)read_bytes != buffer_size && read_bytes != 0) {
            handle_recv_error(buffer, read_bytes);
            return false;
        }
    } while (read_bytes == 0);
    return true;
}

bool receive_pin(int sock_fd, Pin* rec_pin) {
    return receive_data(sock_fd, rec_pin, sizeof(*rec_pin));
}
bool receive_not_crooked_pin(Client worker, Pin* rec_pin) {
    assert(is_worker(worker));
    return receive_pin(worker->client_sock_fd, rec_pin);
}
void sharpen_pin(Pin pin) {
    (void)pin;
    uint32_t sleep_time = (uint32_t)rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME) + MIN_SLEEP_TIME;
    sleep(sleep_time);
}
bool send_sharpened_pin(Client worker, Pin pin) {
    assert(is_worker(worker));
    return send_pin(worker->client_sock_fd, &worker->server_sock_addr, pin);
}
bool receive_sharpened_pin(Client worker, Pin* rec_pin) {
    assert(is_worker(worker));
    return receive_pin(worker->client_sock_fd, rec_pin);
}
bool check_sharpened_pin_quality(Pin sharpened_pin) {
    uint32_t sleep_time = (uint32_t)rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME) + MIN_SLEEP_TIME;
    sleep(sleep_time);
    return cos(sharpened_pin.pin_id) >= 0;
}

bool receive_server_log(Client logs_collector, ServerLog* log) {
    return receive_data(logs_collector->client_sock_fd, log, sizeof(*log));
}

ServerCommandResult send_command_to_server(Client manager, const ServerCommand* cmd) {
    assert(manager->type == MANAGER_CLIENT);
    const ssize_t bytes_sent = sendto(manager->client_sock_fd, cmd, sizeof(*cmd), MSG_NOSIGNAL,
                                      (const struct sockaddr*)&manager->server_sock_addr,
                                      sizeof(manager->server_sock_addr));
    if (bytes_sent != sizeof(*cmd)) {
        client_handle_errno("sendto");
        return NO_CONNECTION;
    }

    ServerCommandResult res;
    if (!receive_data(manager->client_sock_fd, &res, sizeof(res))) {
        return NO_CONNECTION;
    }

    return res;
}

#include "worker-tools.h"

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
#include "net-config.h"     

static bool send_worker_type_info(int socket_fd, struct sockaddr_in* server_sock_addr,
                                  WorkerType type) {
    union {
        char bytes[NET_BUFFER_SIZE];
        WorkerType type;
    } buffer;
    buffer.type = type;
    if (sendto(socket_fd, buffer.bytes, sizeof(type), 0, (const struct sockaddr*)server_sock_addr,
               sizeof(*server_sock_addr)) != sizeof(type)) {
        perror("sendto[while sending worker type to the server]");
        return false;
    }

    printf("Sent type '%d' of this worker to the server\n", type);
    return true;
}

static bool connect_to_server(int socket_fd, struct sockaddr_in* server_sock_addr,
                              const char* server_ip, uint16_t server_port, WorkerType type) {
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

    if (!send_worker_type_info(socket_fd, server_sock_addr, type)) {
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

bool init_worker(Worker worker, const char* server_ip, uint16_t server_port, WorkerType type) {
    worker->type           = type;
    worker->worker_sock_fd = create_socket();
    if (worker->worker_sock_fd == -1) {
        return false;
    }

    bool connected = connect_to_server(worker->worker_sock_fd, &worker->server_sock_addr, server_ip,
                                       server_port, type);
    if (!connected) {
        close(worker->worker_sock_fd);
    }

    return connected;
}

void deinit_worker(Worker worker) {
    close(worker->worker_sock_fd);
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

bool worker_should_stop(const Worker worker) {
    char buffer[NET_BUFFER_SIZE] = {0};
    ssize_t bytes_read =
        recv(worker->worker_sock_fd, buffer, sizeof(buffer), MSG_DONTWAIT | MSG_PEEK);
    if (bytes_read < 0) {
        const int errno_val = errno;
        if (errno_val == EAGAIN || errno_val == EWOULDBLOCK) {
            return false;
        }
        perror("recv");
        return true;
    }

    return is_shutdown_message(buffer, (size_t)bytes_read);
}

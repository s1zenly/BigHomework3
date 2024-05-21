#include "parser.h"

#include <arpa/inet.h>   // for inet_pton
#include <netinet/in.h>  // for sockaddr_in, sockaddr_in6
#include <stdbool.h>     // for bool, true, false
#include <stdio.h>       // for fprintf, NULL, stderr
#include <stdlib.h>      // for strtoul
#include <sys/socket.h>  // for AF_INET, AF_INET6

static bool verify_ip(const char* ip_address, bool ip4_only) {
    struct sockaddr_in sa4;
    int res = inet_pton(AF_INET, ip_address, &sa4.sin_addr);
    if (res == 1) {
        return true;
    }
    if (ip4_only) {
        return false;
    }

    struct sockaddr_in6 sa6;
    return inet_pton(AF_INET6, ip_address, &sa6.sin6_addr) == 1;
}

static bool parse_port(const char* port_str, uint16_t* port) {
    char* end_ptr            = NULL;
    unsigned long port_value = strtoul(port_str, &end_ptr, 10);
    *port                    = (uint16_t)port_value;
    bool port_value_overflow = *port != port_value;
    bool parse_error         = end_ptr == NULL || *end_ptr != '\0';
    return !parse_error && !port_value_overflow;
}

ParseResultWorker parse_args_worker(int argc, const char* argv[]) {
    ParseResultWorker res = {
        .ip_address = NULL,
        .port       = 0,
        .status     = PARSE_INVALID_ARGC,
    };
    if (argc != 3) {
        return res;
    }
    res.ip_address = argv[1];
    if (!verify_ip(res.ip_address, true)) {
        res.status = PARSE_INVALID_IP_ADDRESS;
        return res;
    }
    if (!parse_port(argv[2], &res.port)) {
        res.status = PARSE_INVALID_PORT;
        return res;
    }
    res.status = PARSE_SUCCESS;
    return res;
}

ParseResultServer parse_args_server(int argc, const char* argv[]) {
    ParseResultServer res = {
        .port   = 0,
        .status = PARSE_INVALID_ARGC,
    };
    if (argc != 2) {
        return res;
    }
    if (!parse_port(argv[1], &res.port)) {
        res.status = PARSE_INVALID_PORT;
        return res;
    }
    res.status = PARSE_SUCCESS;
    return res;
}

void print_invalid_args_error_worker(ParseStatus status, const char* program_path) {
    const char* error_str;
    switch (status) {
        case PARSE_INVALID_ARGC:
            error_str = "Invalid number of arguments";
            break;
        case PARSE_INVALID_IP_ADDRESS:
            error_str = "Invalid ip address";
            break;
        case PARSE_INVALID_PORT:
            error_str = "Invalid port";
            break;
        default:
            error_str = "Unknown error";
            break;
    }

    fprintf(stderr,
            "CLI args error: %s\n"
            "Usage: %s <server ip address> <server port>\n"
            "Example: %s 127.0.0.1 31457\n",
            error_str, program_path, program_path);
}

void print_invalid_args_error_server(ParseStatus status, const char* program_path) {
    const char* error_str;
    switch (status) {
        case PARSE_INVALID_ARGC:
            error_str = "Invalid number of arguments";
            break;
        case PARSE_INVALID_PORT:
            error_str = "Invalid port";
            break;
        default:
            error_str = "Unknown error";
            break;
    }

    fprintf(stderr,
            "CLI args error: %s\n"
            "Usage: %s <server port>\n"
            "Example: %s 42592\n",
            error_str, program_path, program_path);
}

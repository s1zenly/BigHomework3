#pragma once

#include <stdint.h>  

typedef enum ParseStatus {
    PARSE_SUCCESS,
    PARSE_INVALID_ARGC,
    PARSE_INVALID_IP_ADDRESS,
    PARSE_INVALID_PORT,
} ParseStatus;

typedef struct ParseResultClient {
    const char* ip_address;
    uint16_t port;
    ParseStatus status;
} ParseResultClient;

typedef struct ParseResultServer {
    uint16_t port;
    ParseStatus status;
} ParseResultServer;

ParseResultClient parse_args_client(int argc, const char* argv[]);
ParseResultServer parse_args_server(int argc, const char* argv[]);

void print_invalid_args_error_client(ParseStatus status, const char* program_path);
void print_invalid_args_error_server(ParseStatus status, const char* program_path);

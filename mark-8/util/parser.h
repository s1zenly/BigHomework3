#pragma once

#include <stdint.h>  // for uint16_t

typedef enum ParseStatus {
    PARSE_SUCCESS,
    PARSE_INVALID_ARGC,
    PARSE_INVALID_IP_ADDRESS,
    PARSE_INVALID_PORT,
} ParseStatus;

typedef struct ParseResultWorker {
    const char* ip_address;
    uint16_t port;
    ParseStatus status;
} ParseResultWorker;

typedef struct ParseResultServer {
    uint16_t port;
    ParseStatus status;
} ParseResultServer;

typedef ParseResultWorker ParseResultCollector;

ParseResultWorker parse_args_worker(int argc, const char* argv[]);
ParseResultServer parse_args_server(int argc, const char* argv[]);
static inline ParseResultCollector parse_args_logs_collector(int argc, const char* argv[]) {
    return parse_args_worker(argc, argv);
}

void print_invalid_args_error_worker(ParseStatus status, const char* program_path);
void print_invalid_args_error_server(ParseStatus status, const char* program_path);
static inline void print_invalid_args_error_logs_collector(ParseStatus status,
                                                           const char* program_path) {
    print_invalid_args_error_worker(status, program_path);
}

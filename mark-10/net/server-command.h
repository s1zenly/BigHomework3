#pragma once

#include <stdint.h>

enum { SERVER_COMMAND_MAX_ADDRESS_SIZE = sizeof("2001:0000:130F:0000:0000:09C0:876A:130B") };

typedef struct ServerCommand {
    char ip_address[SERVER_COMMAND_MAX_ADDRESS_SIZE];
    uint16_t port;
    char unused_cmd_align[22];
} ServerCommand;

typedef enum ServerCommandResult {
    SERVER_COMMAND_SUCCESS,
    INVALID_SERVER_COMMAND_ARGS,
    SERVER_INTERNAL_ERROR,
    NO_CONNECTION,
} ServerCommandResult;

static inline const char* server_command_result_to_string(ServerCommandResult res) {
    switch (res) {
        case SERVER_COMMAND_SUCCESS:
            return "SERVER_COMMAND_SUCCESS";
        case INVALID_SERVER_COMMAND_ARGS:
            return "INVALID_SERVER_COMMAND_ARGS";
        case SERVER_INTERNAL_ERROR:
            return "SERVER_INTERNAL_ERROR";
        case NO_CONNECTION:
            return "NO_CONNECTION";
        default:
            return "unknown command result";
    }
}

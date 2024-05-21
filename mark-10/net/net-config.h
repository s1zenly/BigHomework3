#pragma once

#include <stdbool.h>  
#include <stddef.h>   
#include <string.h>   

#include "pin.h"  

static const char SHUTDOWN_MESSAGE[] = "*";
enum {
    SHUTDOWN_MESSAGE_SIZE = sizeof(SHUTDOWN_MESSAGE),
};

static inline bool is_shutdown_message(const char* bytes, size_t size) {
    return size == SHUTDOWN_MESSAGE_SIZE &&
           memcmp(bytes, SHUTDOWN_MESSAGE, SHUTDOWN_MESSAGE_SIZE) == 0;
}

enum {
    NET_BUFFER_SIZE = sizeof(Pin) > SHUTDOWN_MESSAGE_SIZE ? sizeof(Pin) : SHUTDOWN_MESSAGE_SIZE
};

typedef enum ClientType {
    LOGS_COLLECTOR_CLIENT      = 0,
    FIRST_STAGE_WORKER_CLIENT  = 1,
    SECOND_STAGE_WORKER_CLIENT = 2,
    THIRD_STAGE_WORKER_CLIENT  = 3,
    MANAGER_CLIENT             = 4,
    // Invalid type, max value, e.t.c.
    ENUM_TYPE_SENTINEL,
} ClientType;

static inline const char* client_type_to_string(ClientType type) {
    switch (type) {
        case LOGS_COLLECTOR_CLIENT:
            return "logs collector";
        case FIRST_STAGE_WORKER_CLIENT:
            return "first stage worker";
        case SECOND_STAGE_WORKER_CLIENT:
            return "second stage worker";
        case THIRD_STAGE_WORKER_CLIENT:
            return "third stage worker";
        case MANAGER_CLIENT:
            return "manager";
        default:
            return "unknown client";
    }
}

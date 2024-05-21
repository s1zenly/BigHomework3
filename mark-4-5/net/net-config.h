#pragma once

#include <stdbool.h>  
#include <stddef.h>   
#include <string.h>   

#include "pin.h"  

static const char SHUTDOWN_MESSAGE[] = "0x00000";
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

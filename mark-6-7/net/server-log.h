#pragma once

#include "net-config.h"

enum { MAX_SERVER_LOG_SIZE = 512 };

typedef struct ServerLog {
    char message[MAX_SERVER_LOG_SIZE];
} ServerLog;

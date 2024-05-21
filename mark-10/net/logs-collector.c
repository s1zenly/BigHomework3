#include <stdlib.h>

#include "../util/parser.h"
#include "client-tools.h"
#include "net-config.h"
#include "server-log.h"

static void print_server_log(const ServerLog* log) {
    printf("> Received new server log:\n%s\n", log->message);
}

static int start_runtime_loop(Client logs_col) {
    int ret = EXIT_SUCCESS;
    ServerLog log;
    while (!client_should_stop(logs_col)) {
        if (!receive_server_log(logs_col, &log)) {
            ret = EXIT_FAILURE;
            break;
        }

        print_server_log(&log);
    }

    if (ret == EXIT_SUCCESS) {
        printf("Received shutdown signal from the server\n");
    }

    printf("Logs collector is stopping...\n");
    return ret;
}

static int run_logs_collector(const char* server_ip_address, uint16_t server_port) {
    Client logs_col;
    if (!init_client(logs_col, server_ip_address, server_port, LOGS_COLLECTOR_CLIENT)) {
        return EXIT_FAILURE;
    }

    print_client_info(logs_col);
    int ret = start_runtime_loop(logs_col);
    deinit_client(logs_col);
    return ret;
}

int main(int argc, char const* argv[]) {
    ParseResultClient res = parse_args_client(argc, argv);
    if (res.status != PARSE_SUCCESS) {
        print_invalid_args_error_client(res.status, argv[0]);
        return EXIT_FAILURE;
    }

    return run_logs_collector(res.ip_address, res.port);
}

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "../util/parser.h"
#include "client-tools.h"
#include "net-config.h"

static size_t strip_string(char* str) {
    size_t len = strlen(str);
    while (len > 0 && isspace((uint8_t)str[len - 1])) {
        len--;
    }
    str[len] = '\0';
    return len;
}

static bool next_user_command() {
    printf(
        "Enter command:\n"
        "> 1. Disable client\n"
        "> 2. Exit\n"
        "\n"
        "> ");

    uint32_t cmd = 0;
    while (scanf("%u", &cmd) != 1 || !(cmd - 1 <= 1)) {
        printf("Unknown command, please, try again\n> ");
    }

    return cmd == 1;
}

static void get_command_args(ServerCommand* cmd) {
    char ip_address[64] = {0};
    char port[64]       = {0};
    while (true) {
        printf("Enter ip address:\n> ");
        int c;
        while ((c = getchar()) == '\n') {
        }
        ungetc(c, stdin);
        if (fgets(ip_address, sizeof(ip_address), stdin) == NULL) {
            continue;
        }

        printf("Enter port:\n> ");
        while ((c = getchar()) == '\n') {
        }
        ungetc(c, stdin);
        if (fgets(port, sizeof(port), stdin) == NULL) {
            continue;
        }

        strip_string(ip_address);
        strip_string(port);
        ParseResultClient res = parse_args_client(3, (const char* [3]){"", ip_address, port});
        if (res.status == PARSE_SUCCESS) {
            strcpy(cmd->ip_address, res.ip_address);
            cmd->port = res.port;
            break;
        }
    }
}

static void handle_server_response(ServerCommandResult res, int* ret) {
    switch (res) {
        case SERVER_COMMAND_SUCCESS:
            printf("Server response: success\n");
            break;
        case INVALID_SERVER_COMMAND_ARGS:
            printf("Server response: invalid command arguments\n");
            break;
        case SERVER_INTERNAL_ERROR:
            printf("Server response: internal error\n");
            break;
        case NO_CONNECTION:
            printf("No connection to the server\n");
            *ret = EXIT_FAILURE;
            break;
        default:
            printf("Unknown server response: %d\n", res);
            *ret = EXIT_FAILURE;
            break;
    }
}

static int start_runtime_loop(Client manager) {
    int ret                     = EXIT_SUCCESS;
    bool exit_requested_by_user = false;
    while (ret == EXIT_SUCCESS && !client_should_stop(manager)) {
        if (!next_user_command()) {
            exit_requested_by_user = true;
            break;
        }

        ServerCommand cmd = {0};
        get_command_args(&cmd);
        ServerCommandResult resp = send_command_to_server(manager, &cmd);
        handle_server_response(resp, &ret);
    }

    if (ret == EXIT_SUCCESS && !exit_requested_by_user) {
        printf("Received shutdown signal from the server\n");
    }

    printf("Manager is stopping...\n");
    return ret;
}

static int run_manager(const char* server_ip_address, uint16_t server_port) {
    Client manager;
    if (!init_client(manager, server_ip_address, server_port, MANAGER_CLIENT)) {
        return EXIT_FAILURE;
    }

    print_client_info(manager);
    int ret = start_runtime_loop(manager);
    deinit_client(manager);
    return ret;
}

int main(int argc, char const* argv[]) {
    ParseResultClient res = parse_args_client(argc, argv);
    if (res.status != PARSE_SUCCESS) {
        print_invalid_args_error_client(res.status, argv[0]);
        return EXIT_FAILURE;
    }

    return run_manager(res.ip_address, res.port);
}

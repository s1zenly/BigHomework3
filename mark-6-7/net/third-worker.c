#include <stdbool.h>  
#include <stdint.h>   
#include <stdio.h>    
#include <stdlib.h>   

#include "../util/parser.h"  
#include "client-tools.h"
#include "pin.h"  

static void log_received_pin(Pin pin) {
    printf(
        "+------------------------------------------------------------\n"
        "| Third worker received sharpened pin[pin_id=%d]\n"
        "| and started checking it's quality...\n"
        "+------------------------------------------------------------\n",
        pin.pin_id);
}

static void log_sharpened_pin_quality_check(Pin pin, bool is_ok) {
    printf(
        "+------------------------------------------------------------\n"
        "| Third worker's decision:\n"
        "| pin[pin_id=%d] is sharpened %s.\n"
        "+------------------------------------------------------------\n",
        pin.pin_id, (is_ok ? "good enough" : "badly"));
}

static int start_runtime_loop(Client worker) {
    int ret = EXIT_SUCCESS;
    while (!client_should_stop(worker)) {
        Pin pin;
        if (!receive_sharpened_pin(worker, &pin)) {
            ret = EXIT_FAILURE;
            break;
        }
        log_received_pin(pin);

        bool is_ok = check_sharpened_pin_quality(pin);
        log_sharpened_pin_quality_check(pin, is_ok);
    }

    if (ret == EXIT_SUCCESS) {
        printf("Received shutdown signal from the server\n");
    }

    printf("Third worker is stopping...\n");
    return ret;
}

static int run_worker(const char* server_ip_address, uint16_t server_port) {
    Client worker;
    if (!init_client(worker, server_ip_address, server_port, THIRD_STAGE_WORKER_CLIENT)) {
        return EXIT_FAILURE;
    }

    print_client_info(worker);
    int ret = start_runtime_loop(worker);
    deinit_client(worker);
    return ret;
}

int main(int argc, const char* argv[]) {
    ParseResultWorker res = parse_args_worker(argc, argv);
    if (res.status != PARSE_SUCCESS) {
        print_invalid_args_error_worker(res.status, argv[0]);
        return EXIT_FAILURE;
    }

    return run_worker(res.ip_address, res.port);
}

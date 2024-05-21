#include <stdint.h>  
#include <stdio.h>   
#include <stdlib.h>  

#include "../util/parser.h"  
#include "pin.h"             
#include "worker-tools.h"    

static void log_received_pin(Pin pin) {
    printf(
        "+-------------------------------------------------\n"
        "| Second worker received pin[pin_id=%d]\n"
        "| and started sharpening it...\n"
        "+-------------------------------------------------\n",
        pin.pin_id);
}

static void log_sharpened_pin(Pin pin) {
    printf(
        "+-------------------------------------------------\n"
        "| Second worker sharpened pin[pin_id=%d].\n"
        "+-------------------------------------------------\n",
        pin.pin_id);
}

static void log_sent_pin(Pin pin) {
    printf(
        "+-------------------------------------------------\n"
        "| Second worker sent sharpened\n"
        "| pin[pin_id=%d] to the third workers.\n"
        "+-------------------------------------------------\n",
        pin.pin_id);
}

static int start_runtime_loop(Worker worker) {
    int ret = EXIT_SUCCESS;
    while (!worker_should_stop(worker)) {
        Pin pin;
        if (!receive_not_crooked_pin(worker, &pin)) {
            ret = EXIT_FAILURE;
            break;
        }
        log_received_pin(pin);

        sharpen_pin(pin);
        log_sharpened_pin(pin);

        if (worker_should_stop(worker)) {
            break;
        }
        if (!send_sharpened_pin(worker, pin)) {
            ret = EXIT_FAILURE;
            break;
        }
        log_sent_pin(pin);
    }

    if (ret == EXIT_SUCCESS) {
        worker_handle_shutdown_signal();
    }

    printf("Second worker is stopping...\n");
    return ret;
}

static int run_worker(const char* server_ip_address, uint16_t fserver_port) {
    Worker worker;
    if (!init_worker(worker, server_ip_address, fserver_port, SECOND_STAGE_WORKER)) {
        return EXIT_FAILURE;
    }

    print_worker_info(worker);
    int ret = start_runtime_loop(worker);
    deinit_worker(worker);
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

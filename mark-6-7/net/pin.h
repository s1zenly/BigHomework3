#pragma once

#include <assert.h>   
#include <stdbool.h>  
#include <stddef.h>   


typedef struct Pin {
    int pin_id;
} Pin;

enum { PINS_QUEUE_MAX_SIZE = 16 };

typedef struct PinsQueue {
    Pin array[PINS_QUEUE_MAX_SIZE];
    size_t read_pos;
    size_t write_pos;
    size_t queue_size;
} PinsQueue[1];

static inline bool pins_queue_empty(const PinsQueue queue) {
    return queue->queue_size == 0;
}
static inline bool pins_queue_full(const PinsQueue queue) {
    return queue->queue_size == PINS_QUEUE_MAX_SIZE;
}
static inline bool pins_queue_try_put(PinsQueue queue, Pin pin) {
    if (!pins_queue_full(queue)) {
        assert(queue->write_pos < PINS_QUEUE_MAX_SIZE);
        queue->array[queue->write_pos] = pin;
        queue->write_pos               = (queue->write_pos + 1) % PINS_QUEUE_MAX_SIZE;
        queue->queue_size++;
        return true;
    }
    return false;
}
static inline bool pins_queue_try_pop(PinsQueue queue, Pin* pin) {
    if (!pins_queue_empty(queue)) {
        assert(queue->read_pos < PINS_QUEUE_MAX_SIZE);
        *pin            = queue->array[queue->read_pos];
        queue->read_pos = (queue->read_pos + 1) % PINS_QUEUE_MAX_SIZE;
        queue->queue_size--;
        return true;
    }
    return false;
}

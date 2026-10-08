#pragma once

#include <stdint.h>

typedef uint8_t kthread_id_t;

typedef enum {
    KTHREAD_UNUSED,
    KTHREAD_READY,
    KTHREAD_RUNNING,
    KTHREAD_BLOCKED
} kthread_state_t;

typedef struct {
    uint16_t ss;
    uint16_t sp;

    uint8_t state;
    uint8_t id;

    void *stack;
} kthread_t;



typedef void (*kthread_entry_t)(void);

void kthread_init(void);

int kthread_create(kthread_entry_t entry,
                   uint16_t stack_size);

void kthread_yield(void);

void kthread_block(void);
void kthread_wake(kthread_id_t id);

void kthread_exit(void);

/* Called by the timer interrupt stub */
void kthread_schedule(void);




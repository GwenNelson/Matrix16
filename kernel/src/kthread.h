#pragma once

#include <stdint.h>

/* Keep the saved flags in C storage so each asm block leaves SP balanced. */
#define kthread_critical_enter(flags) do { \
    __asm__ volatile ("pushf; popw %0; cli" : "=r" (flags) : : "memory"); \
} while (0)

#define kthread_critical_exit(flags) do { \
    __asm__ volatile ("pushw %0; popf" : : "r" (flags) : "memory", "cc"); \
} while (0)

typedef uint8_t kthread_id_t;

typedef enum kthread_state_t {
    KTHREAD_UNUSED,
    KTHREAD_READY,
    KTHREAD_RUNNING,
    KTHREAD_BLOCKED
} kthread_state_t;

typedef struct kthread_t {
    uint16_t ss;
    uint16_t sp;

    kthread_state_t state;
    uint8_t id;

} kthread_t;

#define KTHREAD_MAX_ID 6

#define KTHREAD_IDLE_ID      0
#define KTHREAD_EVENT_ID     1
#define KTHREAD_USER_TSK0_ID 2
#define KTHREAD_USER_TSK1_ID 3
#define KTHREAD_USER_TSK2_ID 4
#define KTHREAD_USER_TSK3_ID 5
#define KTHREAD_USER_RSV_ID  6 /* this is reserved for now, might be used eventually for the future serial console task */

#define KTHREAD_INVALID_ID 255

typedef void (*kthread_entry_t)(void);

void kthread_init(void);  // set everything up

void kthread_start(void); // actually start scheduling tasks - does not return

int kthread_setup(kthread_id_t id, uint16_t seg, kthread_entry_t entry, uint16_t stack_top);

void kthread_yield(void);

kthread_id_t kthread_cur_thread(void);

void kthread_block(kthread_id_t id);
void kthread_wake(kthread_id_t id);

void kthread_exit(kthread_id_t id);

/* Called by the timer interrupt stub */
void kthread_schedule(void);



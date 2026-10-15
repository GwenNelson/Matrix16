#pragma once

#include <stdbool.h>
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
    uint16_t task_segment;

    kthread_state_t state;
    uint8_t id;
    uint8_t console;
    volatile bool pending_save_menu;
    volatile bool in_syscall;

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
#define KTHREAD_CONSOLE_NONE 255

typedef void (*kthread_entry_t)(void);

void kthread_init(void);  // set everything up

void kthread_start(void); // actually start scheduling tasks - does not return

int kthread_setup(kthread_id_t id, uint16_t seg, kthread_entry_t entry, uint16_t stack_top, uint8_t console);
int kthread_setup_restored(kthread_id_t id, uint16_t seg, uint16_t sp, uint8_t console);

void kthread_yield(void);

kthread_id_t kthread_cur_thread(void);
bool kthread_current_console(uint8_t *console);
kthread_id_t kthread_from_console(uint8_t console);
void kthread_set_pending_save_menu(kthread_id_t id, bool pending);
bool kthread_is_pending_save_menu(kthread_id_t id);
bool kthread_handoff_save_menu(kthread_id_t id);
kthread_id_t kthread_pending_save_menu_target(void);
void kthread_set_in_syscall(kthread_id_t id, bool in_syscall);

void kthread_block(kthread_id_t id);
void kthread_wake(kthread_id_t id);

void kthread_exit(kthread_id_t id);

/* Called by the timer interrupt stub */
void kthread_schedule(void);

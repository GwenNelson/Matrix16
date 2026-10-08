#include "kthread.h"

void kthread_init(void) {
}

int kthread_create(kthread_entry_t entry, uint16_t stack_size) {
	(void)entry;
	(void)stack_size;
	return 0;
}

void kthread_yield(void) {
}

void kthread_block(void) {
}

void kthread_wake(kthread_id_t id) {
	(void)id;
}

void kthread_exit(void) {
}

void kthread_schedule(void) {
}

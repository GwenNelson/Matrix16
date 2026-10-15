#include <stdint.h>

#include "kaux.h"

enum {
	KAUX_FREE,
	KAUX_USER,
	KAUX_KERNEL
};

static volatile uint8_t claim_kind;
static volatile kthread_id_t claim_owner;

static void clear_claim(void) {
	claim_kind = KAUX_FREE;
	claim_owner = KTHREAD_INVALID_ID;
}

bool kaux_claim_user(kthread_id_t owner) {
	uint16_t irq_flags;
	bool claimed = false;

	kthread_critical_enter(irq_flags);
	if(claim_kind == KAUX_FREE) {
		claim_kind = KAUX_USER;
		claim_owner = owner;
		claimed = true;
	}
	kthread_critical_exit(irq_flags);
	return claimed;
}

bool kaux_release_user(kthread_id_t owner) {
	uint16_t irq_flags;
	bool released = false;

	kthread_critical_enter(irq_flags);
	if(claim_kind == KAUX_USER && claim_owner == owner) {
		clear_claim();
		released = true;
	}
	kthread_critical_exit(irq_flags);
	return released;
}

bool kaux_user_claimed_by(kthread_id_t owner) {
	uint16_t irq_flags;
	bool claimed;

	kthread_critical_enter(irq_flags);
	claimed = claim_kind == KAUX_USER && claim_owner == owner;
	kthread_critical_exit(irq_flags);
	return claimed;
}

bool kaux_claim_kernel(kthread_id_t owner) {
	uint16_t irq_flags;
	bool claimed = false;

	kthread_critical_enter(irq_flags);
	if(claim_kind == KAUX_FREE) {
		claim_kind = KAUX_KERNEL;
		claim_owner = owner;
		claimed = true;
	}
	kthread_critical_exit(irq_flags);
	return claimed;
}

bool kaux_kernel_claimed_by(kthread_id_t owner) {
	uint16_t irq_flags;
	bool claimed;

	kthread_critical_enter(irq_flags);
	claimed = claim_kind == KAUX_KERNEL && claim_owner == owner;
	kthread_critical_exit(irq_flags);
	return claimed;
}

void kaux_release_kernel(kthread_id_t owner) {
	uint16_t irq_flags;

	kthread_critical_enter(irq_flags);
	if(claim_kind == KAUX_KERNEL && claim_owner == owner) clear_claim();
	kthread_critical_exit(irq_flags);
}

void kaux_release_task(kthread_id_t owner) {
	uint16_t irq_flags;

	kthread_critical_enter(irq_flags);
	if(claim_kind != KAUX_FREE && claim_owner == owner) clear_claim();
	kthread_critical_exit(irq_flags);
}

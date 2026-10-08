#include <stdbool.h>
#include <stdint.h>

#include "kthread.h"
#include "kmemmap.h"
#include "kconsole.h"

static kthread_t threads[7]; // statically allocated, because we're targeting a fracking 8086 with 512kb RAM

#define KIDLE_STACK_LEN 512
#define KEVENT_STACK_LEN 2048
#define USER_TEST_STACK_LEN 1024

extern void switch_to(uint16_t ss, uint16_t sp, uint16_t *old_ss,
	uint16_t *old_sp, uint16_t first_run); // ASM routine we use for switching

static uint8_t kidle_stack[512]   __attribute__((aligned(2))); // 512 bytes ought to be enough for anyone...
static uint8_t kevent_stack[2048] __attribute__((aligned(2))); // need something a bit bigger for kevent thread

static uint8_t kuser_test_stack[4][USER_TEST_STACK_LEN] __attribute__((aligned(2)));

static void kidle_task(void) {
	for(;;) {
		asm("hlt");
	}
}

static void kevent_task(void) {
	for(;;) {
		kthread_yield();
		asm("hlt"); // the PIT is still too slow for responsive scheduling
	}
}

static bool scheduler_ready    = false;              // guard against scheduling too early
static kthread_id_t cur_thread = KTHREAD_INVALID_ID; // set to something invalid, obviously

void test_task0(void) {
	kconsole_init(PAGENUM_VC0);
	kconsole_switchto(PAGENUM_VC0);
	kconsole_puts(PAGENUM_VC0, "console 0");

	for(;;) {
		kconsole_move_cursor(PAGENUM_VC0, 2, 1);
		kconsole_putc(PAGENUM_VC0, '-');
		kconsole_move_cursor(PAGENUM_VC0, 2, 1);
		kconsole_putc(PAGENUM_VC0, '\\');
		kconsole_move_cursor(PAGENUM_VC0, 2, 1);
		kconsole_putc(PAGENUM_VC0, '|');
		kconsole_move_cursor(PAGENUM_VC0, 2, 1);
		kconsole_putc(PAGENUM_VC0, '/');
	}
}

void test_task1(void) {
	kconsole_init(PAGENUM_VC1);
	kconsole_switchto(PAGENUM_VC1);
	kconsole_puts(PAGENUM_VC1, "console 1");

	for(;;) {
		kconsole_move_cursor(PAGENUM_VC1, 2, 1);
		kconsole_putc(PAGENUM_VC1, '-');
		kconsole_move_cursor(PAGENUM_VC1, 2, 1);
		kconsole_putc(PAGENUM_VC1, '\\');
		kconsole_move_cursor(PAGENUM_VC1, 2, 1);
		kconsole_putc(PAGENUM_VC1, '|');
		kconsole_move_cursor(PAGENUM_VC1, 2, 1);
		kconsole_putc(PAGENUM_VC1, '/');
	}
}

void test_task2(void) {
	kconsole_init(PAGENUM_VC2);
	kconsole_switchto(PAGENUM_VC2);
	kconsole_puts(PAGENUM_VC2, "console 2");

	for(;;) {
		kconsole_move_cursor(PAGENUM_VC2, 2, 1);
		kconsole_putc(PAGENUM_VC2, '-');
		kconsole_move_cursor(PAGENUM_VC2, 2, 1);
		kconsole_putc(PAGENUM_VC2, '\\');
		kconsole_move_cursor(PAGENUM_VC2, 2, 1);
		kconsole_putc(PAGENUM_VC2, '|');
		kconsole_move_cursor(PAGENUM_VC2, 2, 1);
		kconsole_putc(PAGENUM_VC2, '/');
	}
}

void test_task3(void) {
	kconsole_init(PAGENUM_VC3);
	kconsole_switchto(PAGENUM_VC3);
	kconsole_puts(PAGENUM_VC3, "console 3");

	for(;;) {
		kconsole_move_cursor(PAGENUM_VC3, 2, 1);
		kconsole_putc(PAGENUM_VC3, '-');
		kconsole_move_cursor(PAGENUM_VC3, 2, 1);
		kconsole_putc(PAGENUM_VC3, '\\');
		kconsole_move_cursor(PAGENUM_VC3, 2, 1);
		kconsole_putc(PAGENUM_VC3, '|');
		kconsole_move_cursor(PAGENUM_VC3, 2, 1);
		kconsole_putc(PAGENUM_VC3, '/');
	}
}

void kthread_init(void) {
	// let's first setup the idle thread
	kthread_setup(KTHREAD_IDLE_ID, SEG_KERN,&kidle_task, (uint16_t)(&kidle_stack[KIDLE_STACK_LEN-2]));

	// and let's also setup our event thread
	kthread_setup(KTHREAD_EVENT_ID,SEG_KERN,&kevent_task,(uint16_t)(&kevent_stack[KEVENT_STACK_LEN-2]));

	// for now, testing, let's setup the "user" tasks as just kernel threads
	kthread_setup(KTHREAD_USER_TSK0_ID, SEG_KERN, &test_task0,
		(uint16_t)(&kuser_test_stack[0][USER_TEST_STACK_LEN - 2]));
	kthread_setup(KTHREAD_USER_TSK1_ID, SEG_KERN, &test_task1,
		(uint16_t)(&kuser_test_stack[1][USER_TEST_STACK_LEN - 2]));
	kthread_setup(KTHREAD_USER_TSK2_ID, SEG_KERN, &test_task2,
		(uint16_t)(&kuser_test_stack[2][USER_TEST_STACK_LEN - 2]));
	kthread_setup(KTHREAD_USER_TSK3_ID, SEG_KERN, &test_task3,
		(uint16_t)(&kuser_test_stack[3][USER_TEST_STACK_LEN - 2]));

}



void kthread_start(void) {
	scheduler_ready = true;
	for(;;) asm("hlt"); // we wait here, the timer ISR will fire and scheduler will go
}

int kthread_setup(kthread_id_t id, uint16_t seg, kthread_entry_t entry, uint16_t stack_top) {
	// okay, so first we need to setup the stack as it would be expected to look

	// let's start with creating SP
	uint16_t* sp = (uint16_t*)stack_top;

	// The ISR pushes, from the current SP downwards:
	// ax,cx,dx,bx,original-sp,bp,si,di,ds,es
	// the CPU has already pushed:
	// ip,cs,flags
	//
	// In memory, from the resulting SP upwards, that is:
	// es,ds,di,si,bp,original-sp,bx,dx,cx,ax,ip,cs,flags.
	// Build that frame backwards from the top of the stack.

	uint16_t initial_flags = 0x0202;
	uint16_t ip = (uint16_t)entry;
	uint16_t cs = seg;
	uint16_t ds = seg;
	uint16_t es = seg;

	*--sp = initial_flags;
	*--sp = cs;
	*--sp = ip;
	*--sp = 0x000; // AX
	*--sp = 0x000; // CX
	*--sp = 0x000; // DX
	*--sp = 0x000; // BX
	*--sp = 0x000; // original SP, discarded by the ISR epilogue
	*--sp = 0x000; // BP
	*--sp = 0x000; // SI
	*--sp = 0x000; // DI
	*--sp = ds;
	*--sp = es;

	// now we setup the relevant struct
	threads[id].id    = id;
	threads[id].ss    = seg;
	threads[id].sp    = (uint16_t)sp;
	threads[id].state = KTHREAD_READY;

	(void)entry;
	(void)stack_top;
	return 0;
}

void kthread_yield(void) {
	kthread_schedule();
}

void kthread_block(kthread_id_t id) {
	if(id > KTHREAD_MAX_ID)
		return;

	__asm__ volatile ("pushf; cli" ::: "memory");
	threads[id].state = KTHREAD_BLOCKED;

	if(id == cur_thread) {
		kthread_yield();
		return;
	}

	__asm__ volatile ("popf" ::: "memory");
}

void kthread_wake(kthread_id_t id) {
	if(id > KTHREAD_MAX_ID)
		return;

	__asm__ volatile ("pushf; cli" ::: "memory");
	if(threads[id].state == KTHREAD_BLOCKED)
		threads[id].state = KTHREAD_READY;
	__asm__ volatile ("popf" ::: "memory");
}

void kthread_exit(kthread_id_t id) {
}

void kthread_schedule(void) {
	if(!scheduler_ready)
		return;

	uint16_t irq_flags;
	__asm__ volatile ("pushf; popw %0; cli" : "=r" (irq_flags) : : "memory");

	static bool started[KTHREAD_MAX_ID + 1];
	kthread_id_t previous = cur_thread;
	if(previous != KTHREAD_INVALID_ID &&
	   threads[previous].state == KTHREAD_RUNNING)
		threads[previous].state = KTHREAD_READY;

	kthread_id_t candidate;
	kthread_id_t first;
	if(previous == KTHREAD_INVALID_ID) {
		first = KTHREAD_EVENT_ID;
	} else {
		first = previous + 1;
		if(first > KTHREAD_MAX_ID)
			first = KTHREAD_EVENT_ID;
	}

	candidate = first;
	for(uint8_t checked = 0; checked < KTHREAD_MAX_ID; checked++) {
		if(threads[candidate].state == KTHREAD_READY)
			break;
		candidate++;
		if(candidate > KTHREAD_MAX_ID)
			candidate = KTHREAD_EVENT_ID;
	}

	if(threads[candidate].state != KTHREAD_READY)
		candidate = KTHREAD_IDLE_ID;

	threads[candidate].state = KTHREAD_RUNNING;
	if(candidate == previous) {
		__asm__ volatile ("pushw %0; popf" : : "r" (irq_flags) : "memory", "cc");
		return;
	}

	uint16_t first_run = !started[candidate];
	started[candidate] = true;
	cur_thread = candidate;
	switch_to(threads[candidate].ss, threads[candidate].sp,
		previous == KTHREAD_INVALID_ID ? 0 : &threads[previous].ss,
		previous == KTHREAD_INVALID_ID ? 0 : &threads[previous].sp,
		first_run);
	__asm__ volatile ("pushw %0; popf" : : "r" (irq_flags) : "memory", "cc");
}

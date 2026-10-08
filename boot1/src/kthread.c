#include <stdbool.h>
#include <stdint.h>

#include "kthread.h"
#include "kmemmap.h"
#include "kconsole.h"

static kthread_t threads[7]; // statically allocated, because we're targeting a fracking 8086 with 512kb RAM

#define KIDLE_STACK_LEN 512
#define KEVENT_STACK_LEN 2048

extern void switch_to(uint16_t ss, uint16_t sp); // ASM routine we use for switching

static uint8_t kidle_stack[512]   __attribute__((aligned(2))); // 512 bytes ought to be enough for anyone...
static uint8_t kevent_stack[2048] __attribute__((aligned(2))); // need something a bit bigger for kevent thread

static void kidle_task(void) {
	for(;;) {
		asm("hlt");
	}
}

static void kevent_task(void) {
	for(;;) asm("hlt"); // for now, it does nothing
}

static bool scheduler_ready    = false;              // guard against scheduling too early
static kthread_id_t cur_thread = KTHREAD_INVALID_ID; // set to something invalid, obviously

void kthread_init(void) {
	// let's first setup the idle thread
	kthread_setup(KTHREAD_IDLE_ID, SEG_KERN,&kidle_task, (uint16_t)(&kidle_stack[KIDLE_STACK_LEN-2]));

	// and let's also setup our event thread
	kthread_setup(KTHREAD_EVENT_ID,SEG_KERN,&kevent_task,(uint16_t)(&kevent_stack[KEVENT_STACK_LEN-2]));
}

void test_task0(void) {
	// CODEX BEGIN
	// i want this function to use the routines in kconsole.h to the following:
	// 1 - on entry, kconsole_init(PAGENUM_VC0);
	// 2 - kconsole_switchto(PAGENUM_VC0);
	// 3 - display the string "console 0" using kconsole_putc - you may put a simple helper function named kconsole_puts() into kconsole.c and kconsole.h for this purpose, it should take the page num and a char*
	// 4 - do a simple loop using kconsole_move_cursor(PAGENUM_VC0,2,1) and kconsole_putc() to render a simple spinner in the corner of the screen going through these characters: -,\,|,/
	// CODEX END
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
}

void kthread_block(kthread_id_t id) {
}

void kthread_wake(kthread_id_t id) {
	(void)id;
}

void kthread_exit(kthread_id_t id) {
}

void kthread_schedule(void) {
	
}

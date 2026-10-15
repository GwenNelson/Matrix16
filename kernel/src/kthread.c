#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kthread.h"
#include "kmemmap.h"
#include "kconsole.h"
#include "savemenu.h"
#include "utils.h"

static kthread_t threads[7]; // statically allocated, because we're targeting a fracking 8086 with 512kb RAM

#define KIDLE_STACK_LEN 512
#define KEVENT_STACK_LEN 1024
#define USER_KERNEL_STACK_LEN 1024

extern void switch_to(uint16_t ss, uint16_t sp, uint16_t *old_ss,
	uint16_t *old_sp, uint16_t first_run); // ASM routine we use for switching
extern void copy_initial_frame(uint16_t seg, uint16_t offset, const uint16_t *frame);

static uint8_t kidle_stack[KIDLE_STACK_LEN]   __attribute__((aligned(2))); // 512 bytes ought to be enough for anyone...
static uint8_t kevent_stack[KEVENT_STACK_LEN] __attribute__((aligned(2)));

//static uint8_t kuser_test_stack[4][USER_TEST_STACK_LEN] __attribute__((aligned(2)));
static uint8_t user_kernel_stacks[4][USER_KERNEL_STACK_LEN] __attribute__((aligned(2)));

/* Indexed by cur_thread in the interrupt stubs; only user task IDs use these. */
uint16_t user_kernel_stack_tops[KTHREAD_MAX_ID + 1];

static void kidle_task(void) {
	for(;;) {
		asm("hlt");
	}
}

static volatile bool    pending_console_switch = false;
static volatile uint8_t pending_console_pagenum = 0;
static volatile bool save_menu_active;
static volatile bool save_menu_requested;
static volatile kthread_id_t save_menu_target = KTHREAD_INVALID_ID;
static volatile kthread_id_t save_menu_resume_target = KTHREAD_INVALID_ID;
static volatile uint16_t save_menu_resume_sp;

static void finish_save_menu(void) {
	uint16_t irq_flags;
	kthread_critical_enter(irq_flags);
	if(save_menu_target <= KTHREAD_MAX_ID)
		threads[save_menu_target].pending_save_menu = false;
	save_menu_resume_target = save_menu_target;
	save_menu_target = KTHREAD_INVALID_ID;
	save_menu_active = false;
	kthread_critical_exit(irq_flags);
}

static void kevent_task(void) {
	for(;;) {
		uint16_t irq_flags;
		kthread_critical_enter(irq_flags);
		
		bool need_switch = pending_console_switch;
		uint8_t new_pagenum = pending_console_pagenum;
		bool need_save_menu = save_menu_requested;
		kthread_id_t save_target = save_menu_target;
		uint16_t resume_sp = save_menu_resume_sp;
		
		if(need_switch) pending_console_switch = false;
		if(need_save_menu) save_menu_requested = false;
		
		kthread_critical_exit(irq_flags);

		if(need_save_menu && save_target <= KTHREAD_MAX_ID) {
			savemenu_run(save_target, threads[save_target].task_segment,
			             threads[save_target].console, resume_sp);
			finish_save_menu();
			kthread_yield();
		} else if(need_switch) {
			kconsole_switchto(new_pagenum);
		} else {
			kthread_yield();
		}
	}
}

static bool scheduler_ready    = false;              // guard against scheduling too early
kthread_id_t cur_thread = KTHREAD_INVALID_ID; // also read by the interrupt stubs
static bool started[KTHREAD_MAX_ID + 1];

kthread_id_t kthread_cur_thread(void) {
	return cur_thread;
}

bool kthread_current_console(uint8_t *console) {
	if(console == 0 || cur_thread > KTHREAD_MAX_ID) return false;
	if(threads[cur_thread].console == KTHREAD_CONSOLE_NONE) return false;
	*console = threads[cur_thread].console;
	return true;
}

kthread_id_t kthread_from_console(uint8_t console) {
	kthread_id_t id;

	for(id = KTHREAD_USER_TSK0_ID; id <= KTHREAD_USER_TSK3_ID; id++) {
		if(threads[id].state != KTHREAD_UNUSED && threads[id].console == console) return id;
	}

	return KTHREAD_INVALID_ID;
}

void kthread_set_pending_save_menu(kthread_id_t id, bool pending) {
	if(id > KTHREAD_MAX_ID) return;
	if(pending && save_menu_active) return;
	threads[id].pending_save_menu = pending;
}

bool kthread_is_pending_save_menu(kthread_id_t id) {
	if(id > KTHREAD_MAX_ID) return false;
	return threads[id].pending_save_menu;
}

kthread_id_t kthread_pending_save_menu_target(void) {
	for(kthread_id_t id = KTHREAD_USER_TSK0_ID; id <= KTHREAD_USER_TSK3_ID; id++) {
		if(threads[id].pending_save_menu && !threads[id].in_syscall)
			return id;
	}
	return KTHREAD_INVALID_ID;
}

void kthread_set_in_syscall(kthread_id_t id, bool in_syscall) {
	if(id > KTHREAD_MAX_ID) return;
	threads[id].in_syscall = in_syscall;
}

/* Queue the event thread to run the menu while this task remains suspended
 * on its own stack. The scheduler gate keeps every user task paused until the
 * event thread returns from savemenu_run(). */
bool kthread_handoff_save_menu(kthread_id_t id, uint16_t *register_frame) {
	uint16_t irq_flags;
	uint16_t *bridge;
	uint16_t user_frame_sp;
	uint16_t user_ss;

	if(register_frame == NULL) return false;

	if(id < KTHREAD_USER_TSK0_ID || id > KTHREAD_USER_TSK3_ID) return false;
	if(id != cur_thread || threads[id].in_syscall) return false;
	if(!kthread_is_pending_save_menu(id)) return false;
	if(register_frame[11] != threads[id].task_segment) return false;

	/* ENTER_KERNEL placed the original user SS:SP immediately after this
	 * 13-word frame. Copy the completed user context back before the event
	 * thread can serialize the task's memory segment. */
	bridge = register_frame + 13;
	user_frame_sp = bridge[0];
	user_ss = bridge[1];
	if(user_ss != threads[id].task_segment || (user_frame_sp & 1) != 0 ||
	   user_frame_sp > 0xFFE6) return false;

	uint16_t __far *user_frame = (uint16_t __far *)mk_farptr(user_ss, user_frame_sp);
	for(uint8_t i = 0; i < 13; i++) user_frame[i] = register_frame[i];

	kthread_critical_enter(irq_flags);
	if(save_menu_active || !threads[id].pending_save_menu) {
		kthread_critical_exit(irq_flags);
		return false;
	}
	save_menu_target = id;
	save_menu_resume_sp = user_frame_sp;
	save_menu_requested = true;
	save_menu_active = true;
	kthread_critical_exit(irq_flags);

	kthread_yield();
	return true;
}

void kevent_kbd_cb(uint8_t page_num) {
	/* The menu owns the visible page until it returns. Discard Ctrl-Alt-Fx
	 * here rather than applying a delayed page switch after the menu closes. */
	if(save_menu_active) return;
	pending_console_switch  = true;
	pending_console_pagenum = page_num;
}

void kthread_init(void) {
	for(uint8_t i = 0; i < 4; i++) {
		user_kernel_stack_tops[KTHREAD_USER_TSK0_ID + i] = (uint16_t)&user_kernel_stacks[i][USER_KERNEL_STACK_LEN];
	}

	// let's first setup the idle thread
	kthread_setup(KTHREAD_IDLE_ID, SEG_KERN,&kidle_task, (uint16_t)(&kidle_stack[KIDLE_STACK_LEN-2]), KTHREAD_CONSOLE_NONE);

	// and let's also setup our event thread
	kconsole_set_switch_cb(&kevent_kbd_cb);
	kthread_setup(KTHREAD_EVENT_ID,SEG_KERN,&kevent_task,(uint16_t)(&kevent_stack[KEVENT_STACK_LEN-2]), KTHREAD_CONSOLE_NONE);
}

void kthread_start(void) {
	scheduler_ready = true;
	for(;;) asm("hlt"); // we wait here, the timer ISR will fire and scheduler will go
}

int kthread_setup(kthread_id_t id, uint16_t seg, kthread_entry_t entry, uint16_t stack_top, uint8_t console) {
	// okay, so first we need to setup the stack as it would be expected to look

	// Build the frame on our kernel stack, then copy it into the target SS.
	uint16_t frame[13];
	uint16_t *sp = &frame[13];

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
	copy_initial_frame(seg, stack_top - sizeof(frame), frame);

	// now we setup the relevant struct
	threads[id].id    = id;
	threads[id].ss    = seg;
	threads[id].sp    = stack_top - 13 * sizeof(uint16_t);
	threads[id].task_segment = seg;
	threads[id].console = console;
	threads[id].pending_save_menu = false;
	threads[id].in_syscall = false;
	threads[id].state = KTHREAD_READY;
	started[id] = false;

	(void)entry;
	(void)stack_top;
	return 0;
}

int kthread_setup_restored(kthread_id_t id, uint16_t seg, uint16_t sp, uint8_t console) {
	/* SP already points to a complete interrupt frame in the restored task
	 * segment. The first switch restores that frame and IRET obtains IP, CS,
	 * and FLAGS from it. Do not build or copy another frame here. */
	threads[id].id = id;
	threads[id].ss = seg;
	threads[id].sp = sp;
	threads[id].task_segment = seg;
	threads[id].console = console;
	threads[id].pending_save_menu = false;
	threads[id].in_syscall = false;
	threads[id].state = KTHREAD_READY;
	started[id] = false;
	return 0;
}

void kthread_yield(void) {
	kthread_schedule();
}

void kthread_block(kthread_id_t id) {
	if(id > KTHREAD_MAX_ID)
		return;

	uint16_t irq_flags;
	kthread_critical_enter(irq_flags);
	threads[id].state = KTHREAD_BLOCKED;

	if(id == cur_thread)
		kthread_yield();

	kthread_critical_exit(irq_flags);
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

	kthread_id_t previous = cur_thread;
	if(previous != KTHREAD_INVALID_ID &&
	   threads[previous].state == KTHREAD_RUNNING)
		threads[previous].state = KTHREAD_READY;

	kthread_id_t candidate = KTHREAD_INVALID_ID;
	kthread_id_t first;
	if(previous == KTHREAD_INVALID_ID) {
		first = KTHREAD_EVENT_ID;
	} else {
		first = previous + 1;
		if(first > KTHREAD_MAX_ID)
			first = KTHREAD_EVENT_ID;
	}

	/* When a menu closes, resume its task before rotating to another user. */
	if(!save_menu_active && save_menu_resume_target <= KTHREAD_MAX_ID) {
		if(threads[save_menu_resume_target].state == KTHREAD_READY)
			candidate = save_menu_resume_target;
		save_menu_resume_target = KTHREAD_INVALID_ID;
	}

	if(candidate == KTHREAD_INVALID_ID) {
		candidate = first;
		for(uint8_t checked = 0; checked < KTHREAD_MAX_ID; checked++) {
			bool user_task = candidate >= KTHREAD_USER_TSK0_ID && candidate <= KTHREAD_USER_RSV_ID;
			if(threads[candidate].state == KTHREAD_READY && (!save_menu_active || !user_task))
				break;
			candidate++;
			if(candidate > KTHREAD_MAX_ID)
				candidate = KTHREAD_EVENT_ID;
		}
		if(threads[candidate].state != KTHREAD_READY ||
		   (save_menu_active && candidate >= KTHREAD_USER_TSK0_ID))
			candidate = KTHREAD_IDLE_ID;
	}

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

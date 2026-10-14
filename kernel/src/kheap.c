#include "kheap.h"
#include "kpanic.h"

#include <stdint.h>

extern uint8_t kernel_heap_start;
extern uint8_t kernel_heap_end;

static uint16_t heap_next;
static uint8_t heap_initialized;

void kheap_init(void) {
	heap_next = (uint16_t)&kernel_heap_start;
	heap_initialized = 1;
}

uint16_t kheap_alloc(uint16_t size) {
	uint16_t flags;
	uint16_t heap_end = (uint16_t)&kernel_heap_end;
	uint16_t allocation;

	__asm__ volatile ("pushf; popw %0; cli" : "=r" (flags) : : "memory");

	if(!heap_initialized) kpanic("Kernel heap is not initialized");
	if(size == 0xFFFF) kpanic("Kernel heap exhausted");
	if(size & 1) size++;
	if(heap_next > heap_end || size > heap_end - heap_next) kpanic("Kernel heap exhausted");

	allocation = heap_next;
	heap_next += size;

	__asm__ volatile ("pushw %0; popf" : : "r" (flags) : "memory", "cc");
	return allocation;
}

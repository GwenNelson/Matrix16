#pragma once

#include <stdint.h>

typedef struct registers_t {
	uint16_t es;       // Top of stack after segment push
	uint16_t ds;
	uint16_t di;       // pusha structure starts here
	uint16_t si;
	uint16_t bp;
	uint16_t sp;       // Original SP value before pusha
	uint16_t bx;
	uint16_t dx;
	uint16_t cx;
	uint16_t ax;       // Bottom of pusha (lowest memory address)
} __attribute__((packed)) registers_t;

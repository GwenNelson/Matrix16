#pragma once

#include <stdint.h>

void kheap_init(void);
uint16_t kheap_alloc(uint16_t size);

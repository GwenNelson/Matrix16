#pragma once

#include <stdint.h>

extern volatile uint16_t timer_ticks;

void install_isr08(void);
void install_isr09(void);
void install_isr80(void);

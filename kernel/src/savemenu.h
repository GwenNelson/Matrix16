#pragma once

#include <stdint.h>
#include "kthread.h"

/* Saves the task console, calls the menu entry, then restores that console. */
void savemenu_run(kthread_id_t task_id, uint16_t task_segment, uint8_t console,
	             uint16_t resume_sp);

/* Runs on the event thread while user tasks are paused. */
void savemenu_entry(kthread_id_t task_id, uint16_t task_segment, uint8_t console);

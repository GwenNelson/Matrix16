#pragma once

#include <stdint.h>
#include "kthread.h"

/* Runs on the event thread while user tasks are paused. */
void savemenu_entry(kthread_id_t task_id, uint16_t task_segment, uint8_t console);

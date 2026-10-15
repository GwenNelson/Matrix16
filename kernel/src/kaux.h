#pragma once

#include <stdbool.h>

#include "kthread.h"

/* User claims are owned by a task and released automatically when it exits. */
bool kaux_claim_user(kthread_id_t owner);
bool kaux_release_user(kthread_id_t owner);
bool kaux_user_claimed_by(kthread_id_t owner);

/* Kernel claims protect SEG_AUX while task images are restored. */
bool kaux_claim_kernel(kthread_id_t owner);
bool kaux_kernel_claimed_by(kthread_id_t owner);
void kaux_release_kernel(kthread_id_t owner);

void kaux_release_task(kthread_id_t owner);

#pragma once

#include <stdint.h>

void lba2chs(uint16_t lba, uint16_t *cylinder, uint16_t *head,
             uint16_t *sector);
void __far *mk_farptr(uint16_t segment, uint16_t offset);

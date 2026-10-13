#pragma once

#include <stdint.h>

uint16_t bios_read_sector(uint16_t es, uint16_t bx, uint16_t cylinder,
                          uint16_t head, uint16_t sector, uint16_t drive);

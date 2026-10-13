#include <stdint.h>

#include "utils.h"

void lba2chs(uint16_t lba, uint16_t *cylinder, uint16_t *head,
             uint16_t *sector) {
	*cylinder = lba / 18;
	*head     = (lba % 18) / 9;
	*sector   = (lba % 9) + 1;
}

void __far *mk_farptr(uint16_t segment, uint16_t offset) {
	union {
		struct {
			uint16_t offset;
			uint16_t segment;
		} parts;
		void __far *ptr;
	} u;

	u.parts.offset = offset;
	u.parts.segment = segment;
	return u.ptr;
}

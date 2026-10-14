#include "matfs16.h"

matfs16_status_t matfs16_locate_file(
	const char *filename,
	uint8_t drive_no,
	uint16_t *lba
)
{
	(void)filename;
	(void)drive_no;
	(void)lba;

	return MATFS16_ERR_NOT_IMPLEMENTED;
}

matfs16_status_t matfs16_load_block(
	uint8_t drive_no,
	uint16_t lba,
	uint16_t seg,
	uint16_t buf
)
{
	(void)drive_no;
	(void)lba;
	(void)seg;
	(void)buf;

	return MATFS16_ERR_NOT_IMPLEMENTED;
}

matfs16_status_t matfs16_next_block(
	uint8_t drive_no,
	uint16_t lba,
	uint16_t *next
)
{
	(void)drive_no;
	(void)lba;
	(void)next;

	return MATFS16_ERR_NOT_IMPLEMENTED;
}

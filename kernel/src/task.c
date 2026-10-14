#include <stdbool.h>
#include <stdint.h>

#include "matfs16.h"
#include "mtsk16.h"
#include "task.h"
#include "utils.h"

#define TASK_SCREEN_ROWS 25
#define TASK_SCREEN_COLS 80

#if MTSK16_CONSOLE_INPUT_SIZE != KCONSOLE_INPUT_SIZE || MTSK16_CONSOLE_ANSI_PARAMS != KCONSOLE_ANSI_PARAMS
#error "MTSK16 and kconsole state sizes do not match"
#endif

static const char task_magic[MTSK16_MAGIC_SIZE] = MTSK16_MAGIC;

static bool magic_matches(const mtsk16_header_t __far *header) {
	for(uint16_t i = 0; i < MTSK16_MAGIC_SIZE; i++) {
		if(header->magic[i] != task_magic[i]) return false;
	}

	return true;
}

static task_status_t matfs_error(matfs16_status_t status) {
	if(status == MATFS16_ERR_DISK) return TASK_ERR_DISK;
	if(status == MATFS16_ERR_BAD_BLOCKCHAIN) return TASK_ERR_BAD_BLOCKCHAIN;
	return TASK_ERR_INVALID_ARGUMENT;
}

static task_status_t validate_header(const mtsk16_header_t __far *header) {
	if(!magic_matches(header)) return TASK_ERR_BAD_HEADER;
	if(header->version != MTSK16_FORMAT_VERSION) return TASK_ERR_UNSUPPORTED_VERSION;
	if(header->header_size != MTSK16_HEADER_SIZE) return TASK_ERR_BAD_HEADER;
	if(header->flags != MTSK16_FLAG_RELOCATABLE_SEGMENTS) return TASK_ERR_BAD_HEADER;
	if(header->context_format != MTSK16_CONTEXT_ISR_V1) return TASK_ERR_UNSUPPORTED_CONTEXT;
	if(header->memory_encoding != MTSK16_MEMORY_RAW) return TASK_ERR_UNSUPPORTED_ENCODING;
	if(header->screen_mode != MTSK16_SCREEN_TEXT_80X25_COLOR) return TASK_ERR_BAD_HEADER;
	if(header->vram_size != MTSK16_VRAM_SIZE) return TASK_ERR_BAD_HEADER;
	if(header->vram_first_block != MTSK16_VRAM_FIRST_BLOCK) return TASK_ERR_BAD_HEADER;
	if(header->vram_block_count != MTSK16_VRAM_BLOCKS) return TASK_ERR_BAD_HEADER;
	if(header->memory_first_block != MTSK16_MEMORY_FIRST_BLOCK) return TASK_ERR_BAD_HEADER;
	if(header->memory_stored_blocks != MTSK16_MEMORY_BLOCKS) return TASK_ERR_BAD_HEADER;
	if(header->memory_last_block_bytes != MTSK16_MEMORY_LAST_BLOCK_BYTES) return TASK_ERR_BAD_HEADER;
	if(header->memory_decoded_blocks != MTSK16_MEMORY_BLOCKS) return TASK_ERR_BAD_HEADER;
	if(header->console_state_format != MTSK16_CONSOLE_STATE_V1) return TASK_ERR_BAD_HEADER;
	if(header->console_state_size != sizeof(mtsk16_console_state_v1_t)) return TASK_ERR_BAD_HEADER;
	if(header->console.row >= TASK_SCREEN_ROWS || header->console.col >= TASK_SCREEN_COLS) return TASK_ERR_BAD_HEADER;
	if(header->console.saved_row >= TASK_SCREEN_ROWS || header->console.saved_col >= TASK_SCREEN_COLS) return TASK_ERR_BAD_HEADER;
	if(header->console.wrap > 1 || header->console.caps_lock > 1 || header->console.num_lock > 1) return TASK_ERR_BAD_HEADER;
	if(header->console.ansi_state > 3 || header->console.param_index >= MTSK16_CONSOLE_ANSI_PARAMS) return TASK_ERR_BAD_HEADER;
	if(header->console.input_length > MTSK16_CONSOLE_INPUT_SIZE) return TASK_ERR_BAD_HEADER;
	for(uint8_t i = 0; i < MTSK16_CONSOLE_ANSI_PARAMS; i++) {
		if(header->console.params[i] > 999) return TASK_ERR_BAD_HEADER;
	}
	if((header->resume_sp & 1) != 0) return TASK_ERR_INVALID_CONTEXT;
	if(header->resume_sp > MTSK16_MAX_RESUME_SP) return TASK_ERR_INVALID_CONTEXT;

	return TASK_OK;
}

static void copy_console_state(const mtsk16_console_state_v1_t __far *source, kconsole_saved_state_t *destination) {
	destination->row = source->row;
	destination->col = source->col;
	destination->saved_row = source->saved_row;
	destination->saved_col = source->saved_col;
	destination->attribute = source->attribute;
	destination->wrap = source->wrap;
	destination->caps_lock = source->caps_lock;
	destination->num_lock = source->num_lock;
	destination->ansi_state = source->ansi_state;
	destination->param_index = source->param_index;
	for(uint8_t i = 0; i < MTSK16_CONSOLE_ANSI_PARAMS; i++) destination->params[i] = source->params[i];
	destination->input_length = source->input_length;
	for(uint16_t i = 0; i < source->input_length; i++) destination->input[i] = source->input[i];
}

static task_status_t next_required_block(uint8_t drive_no, uint16_t current_lba, uint16_t *next_lba) {
	matfs16_status_t status = matfs16_next_block(drive_no, current_lba, next_lba);

	if(status == MATFS16_END_OF_FILE) return TASK_ERR_TRUNCATED;
	if(status != MATFS16_OK) return matfs_error(status);
	return TASK_OK;
}

task_status_t task_restore(uint8_t drive_no, uint16_t first_lba, const task_buffers_t *buffers, task_state_t *state) {
	matfs16_status_t matfs_status;
	task_status_t status;
	uint16_t current_lba;
	uint16_t next_lba;
	uint16_t resume_sp;
	uint16_t screen_mode;
	uint16_t vram_size;
	uint16_t memory_encoding;

	if(buffers == 0 || state == 0) return TASK_ERR_INVALID_ARGUMENT;
	if(first_lba < MATFS16_DATA_START_LBA || first_lba >= MATFS16_BLOCK_COUNT) return TASK_ERR_INVALID_ARGUMENT;
	if(buffers->scratch.offset > 0xFE00) return TASK_ERR_INVALID_ARGUMENT;
	if(buffers->vram.offset > 0xF000) return TASK_ERR_INVALID_ARGUMENT;

	matfs_status = matfs16_load_block(drive_no, first_lba,
		buffers->scratch.segment, buffers->scratch.offset);
	if(matfs_status != MATFS16_OK) return matfs_error(matfs_status);

	const mtsk16_header_t __far *header =
		(const mtsk16_header_t __far *)mk_farptr(buffers->scratch.segment,
			buffers->scratch.offset);

	status = validate_header(header);
	if(status != TASK_OK) return status;

	resume_sp = header->resume_sp;
	screen_mode = header->screen_mode;
	vram_size = header->vram_size;
	memory_encoding = header->memory_encoding;
	current_lba = first_lba;

	for(uint16_t block = 0; block < MTSK16_VRAM_BLOCKS; block++) {
		status = next_required_block(drive_no, current_lba, &next_lba);
		if(status != TASK_OK) return status;

		current_lba = next_lba;
		matfs_status = matfs16_load_block(drive_no, current_lba,
			buffers->vram.segment,
			buffers->vram.offset + block * MATFS16_BLOCK_SIZE);
		if(matfs_status != MATFS16_OK) return matfs_error(matfs_status);
	}

	for(uint16_t block = 0; block < MTSK16_MEMORY_BLOCKS; block++) {
		status = next_required_block(drive_no, current_lba, &next_lba);
		if(status != TASK_OK) return status;

		current_lba = next_lba;
		matfs_status = matfs16_load_block(drive_no, current_lba,
			buffers->memory_segment, block * MATFS16_BLOCK_SIZE);
		if(matfs_status != MATFS16_OK) return matfs_error(matfs_status);
	}

	matfs_status = matfs16_next_block(drive_no, current_lba, &next_lba);
	if(matfs_status == MATFS16_OK) return TASK_ERR_TRAILING_BLOCKS;
	if(matfs_status != MATFS16_END_OF_FILE) return matfs_error(matfs_status);

	mtsk16_context_isr_v1_t __far *context =
		(mtsk16_context_isr_v1_t __far *)mk_farptr(buffers->memory_segment,
			resume_sp);
	context->es = buffers->memory_segment;
	context->ds = buffers->memory_segment;
	context->cs = buffers->memory_segment;

	state->resume_sp = resume_sp;
	state->screen_mode = screen_mode;
	state->vram_size = vram_size;
	state->memory_encoding = memory_encoding;
	copy_console_state(&header->console, &state->console);

	return TASK_OK;
}

task_status_t task_save(uint8_t drive_no, uint16_t first_lba, const task_buffers_t *buffers, const task_state_t *state) {
	(void)drive_no;
	(void)first_lba;
	(void)buffers;
	(void)state;
	return TASK_ERR_NOT_IMPLEMENTED;
}

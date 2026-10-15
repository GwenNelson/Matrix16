#include <stdbool.h>
#include <stdint.h>

#include "bioscons.h"
#include "kfiles.h"
#include "kthread.h"
#include "kmemmap.h"
#include "lzss.h"
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
static uint8_t task_lzss_history[M16_LZSS_WINDOW_SIZE] SEG_SYS_BSS;

extern void copy_words_between_segments(uint16_t source_seg, uint16_t dest_seg,
	                                    uint16_t word_count);

static uint8_t __far *task_lzss_history_ptr(void) {
	return (uint8_t __far *)mk_farptr(SEG_SYS,
	                                (uint16_t)task_lzss_history);
}

static void report_loaded_block(void) {
	bios_putchar('.', cur_page_no);
}

static bool magic_matches(const mtsk16_header_t __far *header) {
	for(uint16_t i = 0; i < MTSK16_MAGIC_SIZE; i++) {
		if(header->magic[i] != task_magic[i]) return false;
	}

	return true;
}

static task_status_t matfs_error(matfs16_status_t status) {
	if(status == MATFS16_ERR_DISK) return TASK_ERR_DISK;
	if(status == MATFS16_ERR_BAD_BLOCKCHAIN) return TASK_ERR_BAD_BLOCKCHAIN;
	if(status == MATFS16_ERR_NO_SPACE) return TASK_ERR_NO_SPACE;
	return TASK_ERR_INVALID_ARGUMENT;
}

static task_status_t validate_header(const mtsk16_header_t __far *header) {
	if(!magic_matches(header)) return TASK_ERR_BAD_HEADER;
	if(header->version != MTSK16_FORMAT_VERSION) return TASK_ERR_UNSUPPORTED_VERSION;
	if(header->header_size != MTSK16_HEADER_SIZE) return TASK_ERR_BAD_HEADER;
	if(header->flags != MTSK16_FLAG_RELOCATABLE_SEGMENTS) return TASK_ERR_BAD_HEADER;
	if(header->context_format != MTSK16_CONTEXT_ISR_V1) return TASK_ERR_UNSUPPORTED_CONTEXT;
	if(header->memory_encoding != MTSK16_MEMORY_RAW &&
	   header->memory_encoding != MTSK16_MEMORY_LZSS) return TASK_ERR_UNSUPPORTED_ENCODING;
	if(header->screen_mode != MTSK16_SCREEN_TEXT_80X25_COLOR) return TASK_ERR_BAD_HEADER;
	if(header->vram_size != MTSK16_VRAM_SIZE) return TASK_ERR_BAD_HEADER;
	if(header->vram_first_block != MTSK16_VRAM_FIRST_BLOCK) return TASK_ERR_BAD_HEADER;
	if(header->vram_block_count != MTSK16_VRAM_BLOCKS) return TASK_ERR_BAD_HEADER;
	if(header->memory_first_block != MTSK16_MEMORY_FIRST_BLOCK) return TASK_ERR_BAD_HEADER;
	if(header->memory_decoded_blocks != MTSK16_MEMORY_BLOCKS) return TASK_ERR_BAD_HEADER;
	if(header->memory_encoding == MTSK16_MEMORY_RAW) {
		if(header->memory_stored_blocks != MTSK16_MEMORY_BLOCKS) return TASK_ERR_BAD_HEADER;
		if(header->memory_last_block_bytes != MTSK16_MEMORY_LAST_BLOCK_BYTES) return TASK_ERR_BAD_HEADER;
	} else {
		if(header->memory_stored_blocks == 0 || header->memory_stored_blocks > MTSK16_MEMORY_BLOCKS) return TASK_ERR_BAD_HEADER;
		if(header->memory_last_block_bytes == 0 ||
		   header->memory_last_block_bytes > MATFS16_BLOCK_SIZE) return TASK_ERR_BAD_HEADER;
		if(header->memory_stored_blocks == MTSK16_MEMORY_BLOCKS &&
		   header->memory_last_block_bytes == MATFS16_BLOCK_SIZE) return TASK_ERR_BAD_HEADER;
	}
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

static void copy_console_state(const mtsk16_console_state_v1_t __far *source,
	                           kconsole_saved_state_t __far *destination) {
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

static task_status_t load_task_chain_blocks(uint8_t drive_no, uint16_t *previous_lba,
	                                         uint16_t block_count, uint16_t segment,
	                                         uint16_t offset) {
	uint16_t loaded = 0;
	uint16_t current_lba = *previous_lba;

	while(loaded < block_count) {
		uint16_t first_lba;
		uint16_t last_lba;
		uint16_t next_lba;
		uint16_t run_count = 1;
		uint16_t run_limit;
		matfs16_status_t matfs_status;

		matfs_status = matfs16_next_block(drive_no, current_lba, &first_lba);
		if(matfs_status == MATFS16_END_OF_FILE) return TASK_ERR_TRUNCATED;
		if(matfs_status != MATFS16_OK) return matfs_error(matfs_status);

		last_lba = first_lba;
		run_limit = block_count - loaded;
		if(run_limit > 9 - (first_lba % 9)) run_limit = 9 - (first_lba % 9);

		/* Only batch sectors whose chain entries explicitly point to the next LBA. */
		while(run_count < run_limit) {
			matfs_status = matfs16_next_block(drive_no, last_lba, &next_lba);
			if(matfs_status == MATFS16_END_OF_FILE) return TASK_ERR_TRUNCATED;
			if(matfs_status != MATFS16_OK) return matfs_error(matfs_status);
			if(next_lba != last_lba + 1) break;
			last_lba = next_lba;
			run_count++;
		}

		matfs_status = matfs16_load_contiguous_blocks(drive_no, first_lba,
		                                               run_count, segment, offset);
		if(matfs_status != MATFS16_OK) return matfs_error(matfs_status);

		for(uint16_t i = 0; i < run_count; i++) report_loaded_block();
		loaded += run_count;
		current_lba = last_lba;
		offset += run_count * MATFS16_BLOCK_SIZE;
	}

	*previous_lba = current_lba;
	return TASK_OK;
}

task_status_t task_restore(uint8_t drive_no, uint16_t first_lba,
	                       const task_buffers_t *buffers,
	                       task_state_t __far *state) {
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
	report_loaded_block();

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

	status = load_task_chain_blocks(drive_no, &current_lba, MTSK16_VRAM_BLOCKS,
	                                buffers->vram.segment, buffers->vram.offset);
	if(status != TASK_OK) return status;

	if(memory_encoding == MTSK16_MEMORY_RAW) {
		status = load_task_chain_blocks(drive_no, &current_lba, MTSK16_MEMORY_BLOCKS,
		                                buffers->memory_segment, 0);
		if(status != TASK_OK) return status;
	} else {
		uint16_t stored_blocks = header->memory_stored_blocks;
		uint16_t last_block_bytes = header->memory_last_block_bytes;
		uint16_t compressed_size = (stored_blocks - 1) * MATFS16_BLOCK_SIZE + last_block_bytes;
		uint16_t padding_offset = compressed_size;
		uint16_t padding_count = MATFS16_BLOCK_SIZE - last_block_bytes;
		uint8_t __far *compressed = (uint8_t __far *)mk_farptr(SEG_AUX, 0);
		uint8_t __far *padding = (uint8_t __far *)mk_farptr(SEG_AUX, padding_offset);
		m16_lzss_length_t decoded_size = { MTSK16_MEMORY_BLOCKS, 0 };

		status = load_task_chain_blocks(drive_no, &current_lba, stored_blocks,
		                                SEG_AUX, 0);
		if(status != TASK_OK) return status;

		for(uint16_t i = 0; i < padding_count; i++) {
			if(padding[i] != 0) return TASK_ERR_BAD_COMPRESSION;
		}

		if(!m16_lzss_decompress(compressed, compressed_size,
		                        (uint8_t __far *)mk_farptr(buffers->memory_segment, 0),
			                        decoded_size, task_lzss_history_ptr())) {
			return TASK_ERR_BAD_COMPRESSION;
		}
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

static bool task_segment_matches_id(kthread_id_t task_id, uint16_t segment) {
	if(task_id == KTHREAD_USER_TSK0_ID) return segment == SEG_USER0;
	if(task_id == KTHREAD_USER_TSK1_ID) return segment == SEG_USER1;
	if(task_id == KTHREAD_USER_TSK2_ID) return segment == SEG_USER2;
	if(task_id == KTHREAD_USER_TSK3_ID) return segment == SEG_USER3;
	return false;
}

task_status_t task_replace(kthread_id_t task_id, uint8_t drive_no,
	                       uint16_t first_lba, uint16_t task_segment,
	                       uint8_t console, const task_buffers_t *buffers,
	                       task_state_t __far *state) {
	task_buffers_t staged_buffers;
	task_status_t status;
	mtsk16_context_isr_v1_t __far *context;

	if(buffers == 0 || state == 0 || console >= 4 ||
	   task_id == kthread_cur_thread() ||
	   !task_segment_matches_id(task_id, task_segment))
		return TASK_ERR_INVALID_ARGUMENT;

	/* Keep the old task intact until the full file has validated and loaded. */
	staged_buffers = *buffers;
	staged_buffers.memory_segment = SEG_RESV_USER;
	status = task_restore(drive_no, first_lba, &staged_buffers, state);
	if(status != TASK_OK) return status;

	/* The target is frozen outside a syscall, so its handles can be discarded
	 * immediately before committing the new memory image. */
	kfiles_close_task_files(task_id);
	copy_words_between_segments(SEG_RESV_USER, task_segment, 0x8000);

	/* task_restore built the context for the staging segment. Relocate the
	 * saved data segments and return CS to the task's assigned segment. */
	context = (mtsk16_context_isr_v1_t __far *)mk_farptr(task_segment,
	                                                    state->resume_sp);
	context->es = task_segment;
	context->ds = task_segment;
	context->cs = task_segment;

	kthread_setup_restored(task_id, task_segment, state->resume_sp, console);
	return TASK_OK;
}

bool task_terminate(kthread_id_t task_id) {
	if(task_id < KTHREAD_USER_TSK0_ID || task_id > KTHREAD_USER_TSK3_ID ||
	   task_id == kthread_cur_thread() || !kthread_task_is_active(task_id))
		return false;

	kfiles_close_task_files(task_id);
	kthread_exit(task_id);
	return true;
}

task_status_t task_save(uint8_t drive_no, const char *filename,
	                    const task_buffers_t *buffers,
	                    const task_state_t __far *state) {
	matfs16_status_t matfs_status;
	task_status_t status = TASK_OK;
	uint16_t first_lba;
	uint16_t memory_segment_words[3];
	mtsk16_context_isr_v1_t __far *context;
	mtsk16_header_t __far *header;
	uint8_t __far *scratch;
	uint16_t block_index;
	uint16_t offset;

	if(filename == 0 || buffers == 0 || state == 0 || drive_no > 1) return TASK_ERR_INVALID_ARGUMENT;
	if(buffers->memory_segment == SEG_KERN || buffers->scratch.offset > 0xFE00 ||
	   buffers->vram.offset > 0xF000) return TASK_ERR_INVALID_ARGUMENT;
	if((state->resume_sp & 1) != 0 || state->resume_sp > MTSK16_MAX_RESUME_SP) return TASK_ERR_INVALID_CONTEXT;
	if(state->screen_mode != MTSK16_SCREEN_TEXT_80X25_COLOR ||
	   state->vram_size != MTSK16_VRAM_SIZE) return TASK_ERR_INVALID_ARGUMENT;
	if(state->console.row >= TASK_SCREEN_ROWS || state->console.col >= TASK_SCREEN_COLS ||
	   state->console.saved_row >= TASK_SCREEN_ROWS || state->console.saved_col >= TASK_SCREEN_COLS ||
	   state->console.input_length > MTSK16_CONSOLE_INPUT_SIZE ||
	   state->console.wrap > 1 || state->console.caps_lock > 1 ||
	   state->console.num_lock > 1 || state->console.ansi_state > 3 ||
	   state->console.param_index >= MTSK16_CONSOLE_ANSI_PARAMS) return TASK_ERR_BAD_HEADER;
	for(uint8_t i = 0; i < MTSK16_CONSOLE_ANSI_PARAMS; i++)
		if(state->console.params[i] > 999) return TASK_ERR_BAD_HEADER;

	context = (mtsk16_context_isr_v1_t __far *)mk_farptr(
		buffers->memory_segment, state->resume_sp);
	memory_segment_words[0] = context->es;
	memory_segment_words[1] = context->ds;
	memory_segment_words[2] = context->cs;

	/* TSK v1 stores relocatable segment words as zero. Restore the live frame
	 * before returning, including when a disk operation fails. */
	context->es = 0;
	context->ds = 0;
	context->cs = 0;

	/* A raw TSK is exactly 137 sectors, whose full byte length is represented
	 * by the directory's unknown-size marker. */
	matfs_status = matfs16_replace_file_blocks(drive_no, filename,
		MTSK16_TOTAL_BLOCKS, MATFS16_FILE_SIZE_UNKNOWN, &first_lba);
	if(matfs_status != MATFS16_OK) {
		status = matfs_error(matfs_status);
		goto restore_frame;
	}
	matfs_status = matfs16_cache_blockchain(drive_no);
	if(matfs_status != MATFS16_OK) {
		status = matfs_error(matfs_status);
		goto restore_frame;
	}

	scratch = (uint8_t __far *)mk_farptr(buffers->scratch.segment,
	                                    buffers->scratch.offset);
	for(offset = 0; offset < MATFS16_BLOCK_SIZE; offset++) scratch[offset] = 0;
	header = (mtsk16_header_t __far *)scratch;

	header->magic[0] = 'M';
	header->magic[1] = 'T';
	header->magic[2] = 'S';
	header->magic[3] = 'K';
	header->magic[4] = '1';
	header->magic[5] = '6';
	header->version = MTSK16_FORMAT_VERSION;
	header->header_size = MTSK16_HEADER_SIZE;
	header->flags = MTSK16_FLAG_RELOCATABLE_SEGMENTS;
	header->context_format = MTSK16_CONTEXT_ISR_V1;
	header->memory_encoding = MTSK16_MEMORY_RAW;
	header->screen_mode = state->screen_mode;
	header->vram_size = state->vram_size;
	header->vram_first_block = MTSK16_VRAM_FIRST_BLOCK;
	header->vram_block_count = MTSK16_VRAM_BLOCKS;
	header->memory_first_block = MTSK16_MEMORY_FIRST_BLOCK;
	header->memory_stored_blocks = MTSK16_MEMORY_BLOCKS;
	header->memory_last_block_bytes = MTSK16_MEMORY_LAST_BLOCK_BYTES;
	header->memory_decoded_blocks = MTSK16_MEMORY_BLOCKS;
	header->resume_sp = state->resume_sp;
	header->console_state_format = MTSK16_CONSOLE_STATE_V1;
	header->console_state_size = sizeof(mtsk16_console_state_v1_t);
	header->console.row = state->console.row;
	header->console.col = state->console.col;
	header->console.saved_row = state->console.saved_row;
	header->console.saved_col = state->console.saved_col;
	header->console.attribute = state->console.attribute;
	header->console.wrap = state->console.wrap;
	header->console.caps_lock = state->console.caps_lock;
	header->console.num_lock = state->console.num_lock;
	header->console.ansi_state = state->console.ansi_state;
	header->console.param_index = state->console.param_index;
	for(uint8_t i = 0; i < MTSK16_CONSOLE_ANSI_PARAMS; i++)
		header->console.params[i] = state->console.params[i];
	header->console.input_length = state->console.input_length;
	for(uint16_t i = 0; i < MTSK16_CONSOLE_INPUT_SIZE; i++)
		header->console.input[i] = i < state->console.input_length ? state->console.input[i] : 0;

	matfs_status = matfs16_write_file_block(drive_no, first_lba, 0,
		buffers->scratch.segment, buffers->scratch.offset);
	if(matfs_status != MATFS16_OK) {
		status = matfs_error(matfs_status);
		goto restore_frame;
	}

	for(block_index = 0; block_index < MTSK16_VRAM_BLOCKS; block_index++) {
		matfs_status = matfs16_write_file_block(drive_no, first_lba,
			MTSK16_VRAM_FIRST_BLOCK + block_index, buffers->vram.segment,
			buffers->vram.offset + block_index * MATFS16_BLOCK_SIZE);
		if(matfs_status != MATFS16_OK) {
			status = matfs_error(matfs_status);
			goto restore_frame;
		}
	}

	for(block_index = 0; block_index < MTSK16_MEMORY_BLOCKS; block_index++) {
		offset = block_index * MATFS16_BLOCK_SIZE;
		matfs_status = matfs16_write_file_block(drive_no, first_lba,
			MTSK16_MEMORY_FIRST_BLOCK + block_index, buffers->memory_segment,
			offset);
		if(matfs_status != MATFS16_OK) {
			status = matfs_error(matfs_status);
			goto restore_frame;
		}
	}

restore_frame:
	context->es = memory_segment_words[0];
	context->ds = memory_segment_words[1];
	context->cs = memory_segment_words[2];
	return status;
}

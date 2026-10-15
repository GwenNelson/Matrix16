#include <stdbool.h>
#include <stdint.h>

#include "lzss.h"
#include "matfs16.h"
#include "mtsk16.h"
#include "stdio.h"
#include "syscalls.h"

#define COMMAND_LINE ((const char*)0x0100)
#define AUX_SEGMENT 0x7000
#define IO_SIZE 512
#define MAX_TSK_SIZE 0xFFFE
#define MAX_MEMORY_PAYLOAD (MAX_TSK_SIZE - MTSK16_HEADER_SIZE - MTSK16_VRAM_SIZE)
#define INITIAL_FLAGS 0x0202
#define DEFAULT_TEXT_ATTRIBUTE 0x07
#define SCREEN_CELLS 2000

#if defined(__GNUC__)
typedef char link_header_must_be_one_block[sizeof(mtsk16_header_t) == MTSK16_HEADER_SIZE ? 1 : -1];
typedef char link_console_layout_must_match[sizeof(mtsk16_console_state_v2_t) == 277 ? 1 : -1];
typedef char link_context_layout_must_match[sizeof(mtsk16_context_isr_v1_t) == 26 ? 1 : -1];
#endif

typedef union {
	struct {
		uint16_t offset;
		uint16_t segment;
	} parts;
	uint8_t __far *bytes;
} far_pointer_t;

typedef struct {
	uint16_t count;
	uint16_t limit;
} count_sink_t;

typedef struct {
	uint16_t handle;
	uint16_t count;
	uint16_t buffered;
	char bytes[IO_SIZE];
	bool failed;
} file_sink_t;

static char source_path[28];
static char output_path[28];
static uint8_t io_buffer[IO_SIZE];
static uint8_t vram_block[IO_SIZE];
static mtsk16_header_t task_header;

static uint8_t upper_char(uint8_t ch) {
	if(ch >= 'a' && ch <= 'z') return (uint8_t)(ch - ('a' - 'A'));
	return ch;
}

static void skip_spaces(const char **text) {
	while(**text == ' ' || **text == '\t') (*text)++;
}

static bool normalize_path(const char *input, char *output, char default_drive) {
	uint16_t length = 0;
	uint16_t out = 0;
	char drive = default_drive;

	skip_spaces(&input);
	while(input[length] != '\0' && input[length] != ' ' && input[length] != '\t') length++;
	if(length == 0) return false;
	if(length >= 2 && input[1] == ':') {
		drive = upper_char(input[0]);
		if((drive != 'A' && drive != 'B') || length == 2) return false;
		input += 2;
		length -= 2;
	}
	output[out++] = drive;
	output[out++] = ':';
	if(length == 0 || length > MATFS16_FILENAME_MAXLEN - 1 || out + length >= 28) return false;
	for(uint16_t i = 0; i < length; i++) {
		if(input[i] == ':' || input[i] == '"' || input[i] == '\'') return false;
		output[out++] = (char)upper_char(input[i]);
	}
	output[out] = '\0';
	return true;
}

static bool parse_command_line(void) {
	const char *p = COMMAND_LINE;
	char first[28];
	char second[28];
	char *(*getenv_vector)(const char *) = (char *(*)(const char *))0x0006;
	char *cwd = getenv_vector("CWD");
	char default_drive = 'A';
	uint16_t length = 0;

	if(cwd != 0 && (cwd[0] == 'A' || cwd[0] == 'B') && cwd[1] == ':' && cwd[2] == '\0')
		default_drive = cwd[0];
	skip_spaces(&p);
	while(p[length] != '\0' && p[length] != ' ' && p[length] != '\t') length++;
	if(length == 0 || length >= sizeof(first)) return false;
	for(uint16_t i = 0; i < length; i++) first[i] = p[i];
	first[length] = '\0';
	p += length;
	skip_spaces(&p);
	length = 0;
	while(p[length] != '\0' && p[length] != ' ' && p[length] != '\t') length++;
	if(length == 0 || length >= sizeof(second)) return false;
	for(uint16_t i = 0; i < length; i++) second[i] = p[i];
	second[length] = '\0';
	p += length;
	skip_spaces(&p);
	if(*p != '\0') return false;

	return normalize_path(first, source_path, default_drive) &&
	       normalize_path(second, output_path, default_drive);
}

static bool same_path(void) {
	uint16_t i = 0;
	while(source_path[i] != '\0' && output_path[i] != '\0' &&
	      source_path[i] == output_path[i]) i++;
	return source_path[i] == '\0' && output_path[i] == '\0';
}

static uint8_t __far *aux_bytes(uint16_t offset) {
	far_pointer_t pointer;
	pointer.parts.offset = offset;
	pointer.parts.segment = AUX_SEGMENT;
	return pointer.bytes;
}

static void clear_task_memory(void) {
	uint16_t __far *memory = (uint16_t __far *)aux_bytes(0);
	for(uint16_t i = 0; i < 32768; i++) memory[i] = 0;
}

static bool load_binary_to_aux(uint16_t handle, uint16_t *input_size) {
	uint16_t offset = 0;

	for(;;) {
		uint16_t count = sys_read(handle, (char*)io_buffer, IO_SIZE);
		if(count == 0) break;
		if(count > MTSK16_MAX_INPUT_SIZE - offset) return false;
		for(uint16_t i = 0; i < count; i++) aux_bytes((uint16_t)(offset + i))[0] = io_buffer[i];
		offset += count;
	}
	if(offset == 0) return false;
	*input_size = offset;
	return true;
}

static void build_initial_frame(void) {
	uint16_t __far *frame = (uint16_t __far *)aux_bytes(MTSK16_INITIAL_RESUME_SP);
	for(uint8_t i = 0; i < 13; i++) frame[i] = 0;
	frame[10] = 0;
	frame[12] = INITIAL_FLAGS;
}

static void build_header(uint16_t compressed_size) {
	uint8_t *bytes = (uint8_t*)&task_header;
	uint16_t stored_blocks;
	uint16_t last_bytes;

	for(uint16_t i = 0; i < MTSK16_HEADER_SIZE; i++) bytes[i] = 0;
	stored_blocks = compressed_size / M16_LZSS_BLOCK_SIZE;
	last_bytes = compressed_size % M16_LZSS_BLOCK_SIZE;
	if(last_bytes != 0) stored_blocks++;
	else last_bytes = M16_LZSS_BLOCK_SIZE;

	for(uint8_t i = 0; i < MTSK16_MAGIC_SIZE; i++) task_header.magic[i] = MTSK16_MAGIC[i];
	task_header.version = MTSK16_FORMAT_VERSION;
	task_header.header_size = MTSK16_HEADER_SIZE;
	task_header.flags = MTSK16_FLAG_RELOCATABLE_SEGMENTS;
	task_header.context_format = MTSK16_CONTEXT_ISR_V1;
	task_header.memory_encoding = MTSK16_MEMORY_LZSS;
	task_header.screen_mode = MTSK16_SCREEN_TEXT_80X25_COLOR;
	task_header.vram_size = MTSK16_VRAM_SIZE;
	task_header.vram_first_block = MTSK16_VRAM_FIRST_BLOCK;
	task_header.vram_block_count = MTSK16_VRAM_BLOCKS;
	task_header.memory_first_block = MTSK16_MEMORY_FIRST_BLOCK;
	task_header.memory_stored_blocks = stored_blocks;
	task_header.memory_last_block_bytes = last_bytes;
	task_header.memory_decoded_blocks = MTSK16_MEMORY_BLOCKS;
	task_header.resume_sp = MTSK16_INITIAL_RESUME_SP;
	task_header.console_state_format = MTSK16_CONSOLE_STATE_V2;
	task_header.console_state_size = sizeof(mtsk16_console_state_v2_t);
	task_header.console.attribute = DEFAULT_TEXT_ATTRIBUTE;
	task_header.console.cursor_visible = 1;
}

static bool count_output(void *context, const uint8_t *bytes, uint16_t count) {
	count_sink_t *sink = context;
	(void)bytes;
	if(count > sink->limit - sink->count) return false;
	sink->count += count;
	return true;
}

static bool flush_file_sink(file_sink_t *sink) {
	if(sink->buffered == 0) return true;
	if(sys_write(sink->handle, sink->bytes, sink->buffered) != sink->buffered) {
		sink->failed = true;
		return false;
	}
	sink->buffered = 0;
	return true;
}

static bool write_output(void *context, const uint8_t *bytes, uint16_t count) {
	file_sink_t *sink = context;
	for(uint16_t i = 0; i < count; i++) {
		sink->bytes[sink->buffered++] = (char)bytes[i];
		sink->count++;
		if(sink->buffered == IO_SIZE && !flush_file_sink(sink)) return false;
	}
	return true;
}

static bool write_screen(file_sink_t *sink) {
	for(uint8_t block = 0; block < MTSK16_VRAM_BLOCKS; block++) {
		for(uint16_t cell = 0; cell < IO_SIZE / 2; cell++) {
			uint16_t screen_cell = (uint16_t)block * (IO_SIZE / 2) + cell;
			if(screen_cell < SCREEN_CELLS) {
				vram_block[cell * 2] = ' ';
				vram_block[cell * 2 + 1] = DEFAULT_TEXT_ATTRIBUTE;
			} else {
				vram_block[cell * 2] = 0;
				vram_block[cell * 2 + 1] = 0;
			}
		}
		if(sys_write(sink->handle, (const char*)vram_block, IO_SIZE) != IO_SIZE) return false;
	}
	return true;
}

static void report_error(const char *message) {
	printf("LINK: %s\n", message);
}

void link_main(void) {
	uint16_t input_handle = SYSCALL_INVALID_HANDLE;
	uint16_t output_handle = SYSCALL_INVALID_HANDLE;
	uint16_t input_size = 0;
	uint16_t compressed_size = 0;
	count_sink_t counter;
	file_sink_t writer;
	bool aux_claimed = false;
	bool output_created = false;
	bool success = false;

	if(!parse_command_line()) {
		printf("Usage: LINK input.bin output.tsk\n");
		return;
	}
	if(same_path()) {
		report_error("input and output must be different files");
		return;
	}
	if(sys_claim_aux() != 0) {
		report_error("SEG_AUX is already in use");
		return;
	}
	aux_claimed = true;
	clear_task_memory();

	input_handle = sys_open(source_path);
	if(input_handle == SYSCALL_INVALID_HANDLE) {
		report_error("cannot open input file");
		goto done;
	}
	if(!load_binary_to_aux(input_handle, &input_size)) {
		report_error("input is empty, unreadable, or too large");
		goto done;
	}
	if(sys_close(input_handle) != 0) {
		input_handle = SYSCALL_INVALID_HANDLE;
		report_error("cannot close input file");
		goto done;
	}
	input_handle = SYSCALL_INVALID_HANDLE;

	build_initial_frame();
	counter.count = 0;
	counter.limit = MAX_MEMORY_PAYLOAD;
	if(!m16_lzss_compress_to_sink(aux_bytes(0),
	                              (m16_lzss_length_t){MTSK16_MEMORY_BLOCKS, 0},
	                              count_output, &counter, &compressed_size) ||
	   compressed_size == 0) {
		report_error("compressed TSK would exceed the filesystem write limit");
		goto done;
	}

	output_handle = sys_create(output_path);
	if(output_handle == SYSCALL_INVALID_HANDLE) {
		report_error("cannot create output (it may already exist)");
		goto done;
	}
	output_created = true;
	build_header(compressed_size);
	writer.handle = output_handle;
	writer.count = 0;
	writer.buffered = 0;
	writer.failed = false;
	if(sys_write(output_handle, (const char*)&task_header, MTSK16_HEADER_SIZE) != MTSK16_HEADER_SIZE ||
	   !write_screen(&writer) ||
	   !m16_lzss_compress_to_sink(aux_bytes(0),
	                              (m16_lzss_length_t){MTSK16_MEMORY_BLOCKS, 0},
	                              write_output, &writer, &compressed_size) ||
	   !flush_file_sink(&writer) || writer.count != counter.count) {
		report_error("write failed; removing incomplete output");
		goto done;
	}
	success = true;

 done:
	if(input_handle != SYSCALL_INVALID_HANDLE) (void)sys_close(input_handle);
	if(output_handle != SYSCALL_INVALID_HANDLE) {
		if(sys_close(output_handle) != 0) success = false;
		output_handle = SYSCALL_INVALID_HANDLE;
	}
	if(output_created && !success) (void)sys_delete(output_path);
	if(aux_claimed) (void)sys_release_aux();
	if(success) printf("LINK: created %s from %u bytes (%u compressed)\n",
	                   output_path, input_size, counter.count);
}

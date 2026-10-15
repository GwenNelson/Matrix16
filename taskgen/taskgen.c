#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lzss.h"
#include "mtsk16.h"

#define TASK_MEMORY_SIZE (MTSK16_MEMORY_BLOCKS * MTSK16_MEMORY_LAST_BLOCK_BYTES)
#define VISIBLE_SCREEN_CELLS (80 * 25)
#define DEFAULT_TEXT_ATTRIBUTE 0x07
#define INITIAL_FLAGS 0x0202

_Static_assert(sizeof(mtsk16_header_t) == MTSK16_HEADER_SIZE,
	"mtsk16_header_t must occupy one disk block");
_Static_assert(sizeof(mtsk16_console_state_v2_t) == 277,
	"MTSK16 console state layout has changed");
_Static_assert(sizeof(mtsk16_context_isr_v1_t) == 26,
	"MTSK16 ISR context layout has changed");

static void write_u16(uint8_t *buffer, size_t offset, uint16_t value) {
	buffer[offset] = (uint8_t)(value & 0xff);
	buffer[offset + 1] = (uint8_t)(value >> 8);
}

static void write_header_u16(uint8_t *header, size_t offset, uint16_t value) {
	write_u16(header, offset, value);
}

static int read_input(const char *path, uint8_t *memory) {
	FILE *input = fopen(path, "rb");
	long length;

	if(input == NULL) {
		fprintf(stderr, "taskgen: cannot open %s\n", path);
		return 0;
	}

	if(fseek(input, 0, SEEK_END) != 0) {
		fprintf(stderr, "taskgen: cannot seek %s\n", path);
		fclose(input);
		return 0;
	}

	length = ftell(input);
	if(length < 0) {
		fprintf(stderr, "taskgen: cannot determine the size of %s\n", path);
		fclose(input);
		return 0;
	}

	if((unsigned long)length > MTSK16_MAX_INPUT_SIZE) {
		fprintf(stderr,
			"taskgen: %s is too large; the maximum input size is %u bytes\n",
			path, MTSK16_MAX_INPUT_SIZE);
		fclose(input);
		return 0;
	}

	if(fseek(input, 0, SEEK_SET) != 0) {
		fprintf(stderr, "taskgen: cannot rewind %s\n", path);
		fclose(input);
		return 0;
	}

	if(length != 0 && fread(memory, 1, (size_t)length, input) != (size_t)length) {
		fprintf(stderr, "taskgen: cannot read %s\n", path);
		fclose(input);
		return 0;
	}

	if(fclose(input) != 0) {
		fprintf(stderr, "taskgen: cannot close %s\n", path);
		return 0;
	}

	return 1;
}

static void build_header(uint8_t *header, uint16_t memory_encoding,
	                     uint16_t stored_blocks, uint16_t last_block_bytes) {
	memcpy(header + offsetof(mtsk16_header_t, magic), MTSK16_MAGIC,
		MTSK16_MAGIC_SIZE);
	write_header_u16(header, offsetof(mtsk16_header_t, version),
		MTSK16_FORMAT_VERSION);
	write_header_u16(header, offsetof(mtsk16_header_t, header_size),
		MTSK16_HEADER_SIZE);
	write_header_u16(header, offsetof(mtsk16_header_t, flags),
		MTSK16_FLAG_RELOCATABLE_SEGMENTS);
	write_header_u16(header, offsetof(mtsk16_header_t, context_format),
		MTSK16_CONTEXT_ISR_V1);
	write_header_u16(header, offsetof(mtsk16_header_t, memory_encoding),
		memory_encoding);
	write_header_u16(header, offsetof(mtsk16_header_t, screen_mode),
		MTSK16_SCREEN_TEXT_80X25_COLOR);
	write_header_u16(header, offsetof(mtsk16_header_t, vram_size),
		MTSK16_VRAM_SIZE);
	write_header_u16(header, offsetof(mtsk16_header_t, vram_first_block),
		MTSK16_VRAM_FIRST_BLOCK);
	write_header_u16(header, offsetof(mtsk16_header_t, vram_block_count),
		MTSK16_VRAM_BLOCKS);
	write_header_u16(header, offsetof(mtsk16_header_t, memory_first_block),
		MTSK16_MEMORY_FIRST_BLOCK);
	write_header_u16(header, offsetof(mtsk16_header_t, memory_stored_blocks),
		stored_blocks);
	write_header_u16(header, offsetof(mtsk16_header_t, memory_last_block_bytes),
		last_block_bytes);
	write_header_u16(header, offsetof(mtsk16_header_t, memory_decoded_blocks),
		MTSK16_MEMORY_BLOCKS);
	write_header_u16(header, offsetof(mtsk16_header_t, resume_sp),
		MTSK16_INITIAL_RESUME_SP);
	write_header_u16(header, offsetof(mtsk16_header_t, console_state_format),
		MTSK16_CONSOLE_STATE_V2);
	write_header_u16(header, offsetof(mtsk16_header_t, console_state_size),
		sizeof(mtsk16_console_state_v2_t));
	header[offsetof(mtsk16_header_t, console) +
		offsetof(mtsk16_console_state_v2_t, attribute)] = DEFAULT_TEXT_ATTRIBUTE;
	header[offsetof(mtsk16_header_t, console) +
		offsetof(mtsk16_console_state_v2_t, cursor_visible)] = 1;
}

static void build_vram(uint8_t *vram) {
	for(size_t cell = 0; cell < VISIBLE_SCREEN_CELLS; cell++) {
		vram[cell * 2] = ' ';
		vram[cell * 2 + 1] = DEFAULT_TEXT_ATTRIBUTE;
	}
}

static void build_initial_context(uint8_t *memory) {
	uint8_t *context = memory + MTSK16_INITIAL_RESUME_SP;

	/* Segment words remain zero until task_restore selects a segment. */
	write_u16(context, offsetof(mtsk16_context_isr_v1_t, ip), 0);
	write_u16(context, offsetof(mtsk16_context_isr_v1_t, flags), INITIAL_FLAGS);
}

static int write_output(const char *path, const uint8_t *header,
	const uint8_t *vram, const uint8_t *memory, size_t memory_size) {
	FILE *output = fopen(path, "wb");
	int ok = 1;

	if(output == NULL) {
		fprintf(stderr, "taskgen: cannot create %s\n", path);
		return 0;
	}

	if(fwrite(header, 1, MTSK16_HEADER_SIZE, output) != MTSK16_HEADER_SIZE) ok = 0;
	if(ok && fwrite(vram, 1, MTSK16_VRAM_SIZE, output) != MTSK16_VRAM_SIZE) ok = 0;
	if(ok && fwrite(memory, 1, memory_size, output) != memory_size) ok = 0;
	if(fclose(output) != 0) ok = 0;

	if(!ok) {
		fprintf(stderr, "taskgen: cannot write %s\n", path);
		remove(path);
	}

	return ok;
}

int main(int argc, char **argv) {
	uint8_t *header;
	uint8_t *vram;
	uint8_t *memory;
	uint8_t *compressed;
	uint16_t memory_encoding = MTSK16_MEMORY_RAW;
	uint16_t stored_blocks = MTSK16_MEMORY_BLOCKS;
	uint16_t last_block_bytes = MTSK16_MEMORY_LAST_BLOCK_BYTES;
	uint16_t compressed_size = 0;
	size_t output_memory_size = TASK_MEMORY_SIZE;
	int ok;

	if(argc != 3) {
		fprintf(stderr, "usage: %s INPUT.BIN OUTPUT.TSK\n", argv[0]);
		return 1;
	}

	header = calloc(1, MTSK16_HEADER_SIZE);
	vram = calloc(1, MTSK16_VRAM_SIZE);
	memory = calloc(1, TASK_MEMORY_SIZE);
	compressed = malloc(TASK_MEMORY_SIZE - 1);
	if(header == NULL || vram == NULL || memory == NULL || compressed == NULL) {
		fprintf(stderr, "taskgen: out of memory\n");
		free(header);
		free(vram);
		free(memory);
		free(compressed);
		return 1;
	}

	ok = read_input(argv[1], memory);
	if(ok) {
		m16_lzss_length_t memory_length = { MTSK16_MEMORY_BLOCKS, 0 };

		/* The initial interrupt frame is part of the memory image. Initialize it
		 * before compression so the compressed payload contains the right FLAGS. */
		build_initial_context(memory);

		if(m16_lzss_compress(memory, memory_length, compressed,
		                     TASK_MEMORY_SIZE - 1, &compressed_size)) {
			uint16_t remainder = compressed_size % M16_LZSS_BLOCK_SIZE;
			memory_encoding = MTSK16_MEMORY_LZSS;
			stored_blocks = compressed_size / M16_LZSS_BLOCK_SIZE;
			if(remainder == 0) last_block_bytes = M16_LZSS_BLOCK_SIZE;
			else {
				stored_blocks++;
				last_block_bytes = remainder;
			}
			output_memory_size = compressed_size;
		}

		build_header(header, memory_encoding, stored_blocks, last_block_bytes);
		build_vram(vram);
		ok = write_output(argv[2], header, vram,
		                  memory_encoding == MTSK16_MEMORY_LZSS ? compressed : memory,
		                  output_memory_size);
	}

	free(header);
	free(vram);
	free(memory);
	free(compressed);
	return ok ? 0 : 1;
}

#pragma once

#include <stdint.h>

#define MTSK16_MAGIC "MTSK16"
#define MTSK16_MAGIC_SIZE 6

#define MTSK16_FORMAT_VERSION 1
#define MTSK16_HEADER_SIZE 512

#define MTSK16_VRAM_SIZE 4096
#define MTSK16_VRAM_FIRST_BLOCK 1
#define MTSK16_VRAM_BLOCKS 8

#define MTSK16_MEMORY_FIRST_BLOCK 9
#define MTSK16_MEMORY_BLOCKS 128
#define MTSK16_MEMORY_LAST_BLOCK_BYTES 512

#define MTSK16_TOTAL_BLOCKS 137

#define MTSK16_INITIAL_RESUME_SP 0xFFE4
#define MTSK16_MAX_RESUME_SP 0xFFE6
#define MTSK16_MAX_INPUT_SIZE MTSK16_INITIAL_RESUME_SP

#define MTSK16_FLAG_RELOCATABLE_SEGMENTS 0x0001

enum {
	MTSK16_CONTEXT_ISR_V1 = 1
};

enum {
	MTSK16_MEMORY_RAW = 0,
	MTSK16_MEMORY_LZSS = 1
};

enum {
	MTSK16_SCREEN_TEXT_80X25_COLOR = 1
};

/*
 * The first block of every task file. Block numbers in this structure are
 * relative to the start of the file, rather than absolute disk LBAs.
 */
typedef struct __attribute__((packed)) mtsk16_header_t {
	char magic[MTSK16_MAGIC_SIZE];
	uint16_t version;
	uint16_t header_size;
	uint16_t flags;
	uint16_t context_format;
	uint16_t memory_encoding;
	uint16_t screen_mode;
	uint16_t vram_size;
	uint16_t vram_first_block;
	uint16_t vram_block_count;
	uint16_t memory_first_block;
	uint16_t memory_stored_blocks;
	uint16_t memory_last_block_bytes;
	uint16_t memory_decoded_blocks;
	uint16_t resume_sp;
	uint16_t cursor_row;
	uint16_t cursor_col;
	uint8_t reserved[474];
} mtsk16_header_t;

/*
 * Context format MTSK16_CONTEXT_ISR_V1. This is the frame expected by the
 * existing interrupt-return path, starting at header.resume_sp.
 */
typedef struct __attribute__((packed)) mtsk16_context_isr_v1_t {
	uint16_t es;
	uint16_t ds;
	uint16_t di;
	uint16_t si;
	uint16_t bp;
	uint16_t saved_sp;
	uint16_t bx;
	uint16_t dx;
	uint16_t cx;
	uint16_t ax;
	uint16_t ip;
	uint16_t cs;
	uint16_t flags;
} mtsk16_context_isr_v1_t;

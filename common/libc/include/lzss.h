#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Stream format: each group starts with a control byte whose bits are read
 * least-significant first. A set bit is one literal byte; a clear bit is a
 * two-byte match. The first match byte holds distance-minus-one bits 0..7.
 * The low nibble of the second byte holds distance-minus-one bits 8..11, and
 * its high nibble holds length-minus-three. Thus the window is 4096 bytes and
 * match lengths are 3..18. There is no end marker: the caller supplies the
 * exact decoded size, and the decoder rejects unused trailing input bytes.
 */
#define M16_LZSS_WINDOW_SIZE 4096
#define M16_LZSS_MIN_MATCH 3
#define M16_LZSS_MAX_MATCH 18
#define M16_LZSS_BLOCK_SIZE 512
#define M16_LZSS_MAX_BLOCKS 128

/*
 * A byte length represented without needing a 32-bit integer. The value is
 * blocks * 512 + tail_bytes. tail_bytes must be less than 512. This represents
 * lengths from zero through exactly 65536 bytes.
 */
typedef struct m16_lzss_length_t {
	uint16_t blocks;
	uint16_t tail_bytes;
} m16_lzss_length_t;

#if defined(__ia16__)
#define M16_LZSS_FAR __far
#else
#define M16_LZSS_FAR
#endif

/*
 * Compress one buffer. Returns true only when the output fits and is strictly
 * smaller than the input. output_size is set to zero on failure. A 64 KiB
 * input is represented by { 128, 0 }; output_capacity is at most 65535.
 */
#if !defined(__ia16__)
bool m16_lzss_compress(const uint8_t M16_LZSS_FAR *input,
	                       m16_lzss_length_t input_length,
	                       uint8_t M16_LZSS_FAR *output,
	                       uint16_t output_capacity,
	                       uint16_t *output_size);
#endif

/*
 * Decompress exactly input_size bytes into the requested output length.
 * history must point to a 4096-byte caller-owned work area. Output lengths up
 * to 65536 bytes are supported. On failure, output may contain a partial
 * result and must be discarded.
 */
bool m16_lzss_decompress(const uint8_t M16_LZSS_FAR *input,
	                     uint16_t input_size,
	                     uint8_t M16_LZSS_FAR *output,
	                     m16_lzss_length_t output_length,
	                     uint8_t M16_LZSS_FAR *history);

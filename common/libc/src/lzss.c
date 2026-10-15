#include "lzss.h"

static bool valid_length(m16_lzss_length_t length) {
	if(length.blocks > M16_LZSS_MAX_BLOCKS) return false;
	if(length.tail_bytes >= M16_LZSS_BLOCK_SIZE) return false;
	if(length.blocks == M16_LZSS_MAX_BLOCKS && length.tail_bytes != 0) return false;
	return true;
}

static bool input_has_bytes(uint16_t total_blocks, uint16_t tail_bytes,
	                        uint16_t position_blocks, uint16_t position_offset,
	                        uint16_t wanted) {
	uint16_t whole_blocks;
	uint16_t bytes_in_current_block;

	if(position_blocks > total_blocks) return false;
	if(position_blocks == total_blocks) {
		if(position_offset > tail_bytes) return false;
		return wanted <= tail_bytes - position_offset;
	}

	whole_blocks = total_blocks - position_blocks;
	bytes_in_current_block = M16_LZSS_BLOCK_SIZE - position_offset;
	if(whole_blocks > 1) return true;
	return wanted <= bytes_in_current_block + tail_bytes;
}

static void advance_position(uint16_t count, uint16_t *blocks, uint16_t *offset) {
	*offset += count;
	while(*offset >= M16_LZSS_BLOCK_SIZE) {
		*offset -= M16_LZSS_BLOCK_SIZE;
		(*blocks)++;
	}
}

bool m16_lzss_compress_to_sink(const uint8_t M16_LZSS_FAR *input,
	                           m16_lzss_length_t input_length,
	                           m16_lzss_sink_t sink, void *context,
	                           uint16_t *output_size) {
	uint16_t input_blocks = 0;
	uint16_t input_offset = 0;
	uint16_t input_index = 0;
	uint16_t history_size = 0;
	uint16_t output_position = 0;
	uint16_t original_size = 0;
	bool input_is_64k;

	if(output_size == 0 || sink == 0) return false;
	*output_size = 0;
	if(input == 0 || !valid_length(input_length)) return false;
	if(input_length.blocks == 0 && input_length.tail_bytes == 0) return false;

	input_is_64k = input_length.blocks == M16_LZSS_MAX_BLOCKS;
	if(!input_is_64k) {
		original_size = input_length.blocks * M16_LZSS_BLOCK_SIZE + input_length.tail_bytes;
	}

	while(input_has_bytes(input_length.blocks, input_length.tail_bytes,
	                      input_blocks, input_offset, 1)) {
		uint8_t group[17];
		uint8_t group_size = 1;
		uint8_t flags = 0;
		uint8_t token;

		for(token = 0; token < 8 &&
		    input_has_bytes(input_length.blocks, input_length.tail_bytes,
		                    input_blocks, input_offset, 1); token++) {
			uint16_t available = 0;
			uint16_t distance;
			uint16_t best_distance = 0;
			uint16_t best_length = 0;

			while(available < M16_LZSS_MAX_MATCH &&
			      input_has_bytes(input_length.blocks, input_length.tail_bytes,
			                      input_blocks, input_offset, available + 1)) {
				available++;
			}

			for(distance = 1; distance <= history_size; distance++) {
				uint16_t candidate_index = input_index - distance;
				uint16_t match_length = 0;

				while(match_length < available &&
				      input[candidate_index + match_length] ==
				      input[input_index + match_length]) {
					match_length++;
				}
				if(match_length > best_length) {
					best_length = match_length;
					best_distance = distance;
					if(best_length == available) break;
				}
			}

			if(best_length >= M16_LZSS_MIN_MATCH) {
				uint16_t distance_minus_one = best_distance - 1;
				uint16_t length_minus_three = best_length - M16_LZSS_MIN_MATCH;
				group[group_size++] = (uint8_t)distance_minus_one;
				group[group_size++] = (uint8_t)((distance_minus_one >> 8) |
				                               (length_minus_three << 4));
			} else {
				flags |= (uint8_t)(1u << token);
				group[group_size++] = input[input_index];
				best_length = 1;
			}

			input_index += best_length;
			advance_position(best_length, &input_blocks, &input_offset);
			if(history_size < M16_LZSS_WINDOW_SIZE) {
				if(best_length >= M16_LZSS_WINDOW_SIZE - history_size) {
					history_size = M16_LZSS_WINDOW_SIZE;
				} else {
					history_size += best_length;
				}
			}
		}

		group[0] = flags;
		if(group_size > 0xFFFF - output_position || !sink(context, group, group_size)) return false;
		output_position += group_size;
	}

	if(!input_is_64k && output_position >= original_size) return false;
	*output_size = output_position;
	return true;
}

#if !defined(__ia16__)
typedef struct {
	uint8_t *output;
	uint16_t capacity;
	uint16_t position;
} memory_sink_t;

static bool memory_sink(void *context, const uint8_t *bytes, uint16_t count) {
	memory_sink_t *sink = context;
	uint16_t i;
	if(count > sink->capacity - sink->position) return false;
	for(i = 0; i < count; i++) sink->output[sink->position + i] = bytes[i];
	sink->position += count;
	return true;
}

bool m16_lzss_compress(const uint8_t M16_LZSS_FAR *input,
	                   m16_lzss_length_t input_length,
	                   uint8_t M16_LZSS_FAR *output,
	                   uint16_t output_capacity,
	                   uint16_t *output_size) {
	memory_sink_t sink;
	uint16_t encoded_size;

	if(output_size == 0) return false;
	*output_size = 0;
	if(output == 0) return false;
	sink.output = output;
	sink.capacity = output_capacity;
	sink.position = 0;
	if(!m16_lzss_compress_to_sink(input, input_length, memory_sink,
	                              &sink, &encoded_size)) return false;
	*output_size = encoded_size;
	return true;
}
#endif

bool m16_lzss_decompress(const uint8_t M16_LZSS_FAR *input,
	                     uint16_t input_size,
	                     uint8_t M16_LZSS_FAR *output,
	                     m16_lzss_length_t output_length,
	                     uint8_t M16_LZSS_FAR *history) {
	const uint8_t M16_LZSS_FAR *input_position = input;
	uint8_t M16_LZSS_FAR *output_position = output;
	uint16_t input_remaining = input_size;
	uint16_t output_blocks = 0;
	uint16_t output_offset = 0;
	uint16_t history_size = 0;
	uint16_t history_position = 0;

	if((input == 0 && input_size != 0) || output == 0 || history == 0 ||
	   !valid_length(output_length)) return false;

	while(input_remaining != 0 &&
	      input_has_bytes(output_length.blocks, output_length.tail_bytes,
	                      output_blocks, output_offset, 1)) {
		uint8_t flags;
		uint8_t token;

		flags = *input_position++;
		input_remaining--;
		for(token = 0; token < 8 &&
		    input_has_bytes(output_length.blocks, output_length.tail_bytes,
		                    output_blocks, output_offset, 1); token++) {
			uint16_t produced;

			if(flags & (uint8_t)(1u << token)) {
				if(input_remaining == 0) return false;
				uint8_t value = *input_position++;
				*output_position++ = value;
				input_remaining--;
				history[history_position] = value;
				history_position = (history_position + 1) & (M16_LZSS_WINDOW_SIZE - 1);
				produced = 1;
			} else {
				uint16_t distance;
				uint16_t length;
				uint8_t low;
				uint8_t high;

				if(input_remaining < 2) return false;
				low = *input_position++;
				high = *input_position++;
				input_remaining -= 2;
				distance = (uint16_t)low | ((uint16_t)(high & 0x0F) << 8);
				distance++;
				length = (uint16_t)(high >> 4) + M16_LZSS_MIN_MATCH;

				if(distance > history_size ||
				   !input_has_bytes(output_length.blocks, output_length.tail_bytes,
				                   output_blocks, output_offset, length)) return false;

				for(produced = 0; produced < length; produced++) {
					uint16_t history_index;
					uint8_t value;

					if(history_position >= distance) history_index = history_position - distance;
					else history_index = history_position + (M16_LZSS_WINDOW_SIZE - distance);
					value = history[history_index];
					*output_position = value;
					output_position++;
					history[history_position] = value;
					history_position = (history_position + 1) & (M16_LZSS_WINDOW_SIZE - 1);
				}
			}

			advance_position(produced, &output_blocks, &output_offset);
			if(history_size < M16_LZSS_WINDOW_SIZE) {
				if(produced >= M16_LZSS_WINDOW_SIZE - history_size) {
					history_size = M16_LZSS_WINDOW_SIZE;
				} else {
					history_size += produced;
				}
			}
		}
	}

	return !input_has_bytes(output_length.blocks, output_length.tail_bytes,
	                         output_blocks, output_offset, 1) && input_remaining == 0;
}

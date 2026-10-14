#include <stdint.h>

#include <syscalls.inc>

#define LINE_LENGTH 80

static void sys_write(const char* buffer, uint16_t length) {
	uint16_t ax = SYS_WRITE;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)1), "c" (length), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);
}

static uint16_t sys_read(char* buffer, uint16_t length) {
	uint16_t ax = SYS_READ;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)0), "c" (length), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);

	return ax;
}

static uint16_t readline(char* line, uint16_t capacity) {
	uint16_t length = 0;
	uint8_t escape_state = 0;
	char c;

	if(capacity == 0) return 0;

	for(;;) {
		if(sys_read(&c,1) != 1) continue;

		if(escape_state == 1) {
			escape_state = c == '[' ? 2 : 0;
			continue;
		}

		if(escape_state == 2) {
			if((uint8_t)c >= 0x40 && (uint8_t)c <= 0x7E) escape_state = 0;
			continue;
		}

		if((uint8_t)c == 0x1B) {
			escape_state = 1;
			continue;
		}

		if(c == '\r') continue;
		if(c == '\n') {
			line[length] = '\0';
			sys_write("\r\n",2);
			return length;
		}

		if(c == '\b') {
			if(length != 0) {
				length--;
				sys_write("\b \b",3);
			}
			continue;
		}

		if((uint8_t)c < 0x20 || (uint8_t)c == 0x7F) continue;
		if(length == capacity - 1) continue;

		line[length++] = c;
		sys_write(&c,1);
	}
}

int main(void) {
	char line[LINE_LENGTH];

	for(;;) {
		sys_write("Matrix16> ",10);
		readline(line,sizeof(line));
	}
}

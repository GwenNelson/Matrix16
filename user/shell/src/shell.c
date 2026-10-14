#include <stdint.h>

#include <syscalls.h>

#define LINE_LENGTH 80

static uint16_t readline(char* line, uint16_t capacity) {
	uint16_t length = 0;
	uint8_t escape_state = 0;
	char c;

	if(capacity == 0) return 0;

	for(;;) {
		if(sys_read(0,&c,1) != 1) continue;

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
			sys_write(1,"\r\n",2);
			return length;
		}

		if(c == '\b') {
			if(length != 0) {
				length--;
				sys_write(1,"\b \b",3);
			}
			continue;
		}

		if((uint8_t)c < 0x20 || (uint8_t)c == 0x7F) continue;
		if(length == capacity - 1) continue;

		line[length++] = c;
		sys_write(1,&c,1);
	}
}

int main(void) {
	char line[LINE_LENGTH];
	uint16_t console_num;

	console_num = sys_get_console();
	printf("Welcome to Matrix16\n");
	printf("You are on console %u\n",console_num);

	for(;;) {
		sys_write(1,"Matrix16> ",10);
		readline(line,sizeof(line));
	}
}

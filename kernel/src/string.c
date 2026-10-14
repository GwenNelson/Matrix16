#include <stdint.h>

#include "string.h"

int strcmp(const char *a, const char *b) {
	while(*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, uint16_t n) {
	while(n != 0) {
		uint8_t ca = (uint8_t)*a++;
		uint8_t cb = (uint8_t)*b++;

		if(ca != cb)
			return ca - cb;
		if(ca == '\0')
			return 0;
		n--;
	}
	return 0;
}

uint16_t strnlen(const char *s, uint16_t maxlen) {
	uint16_t length = 0;

	while(length < maxlen && s[length] != '\0')
		length++;

	return length;
}

char *strchr(const char *s, int c) {
	uint8_t target = (uint8_t)c;

	for(;;) {
		if((uint8_t)*s == target)
			return (char*)s;
		if(*s == '\0')
			return 0;
		s++;
	}
}

char *strcpy(char *dest, const char *src) {
	char *result = dest;

	while((*dest++ = *src++) != '\0')
		;
	return result;
}

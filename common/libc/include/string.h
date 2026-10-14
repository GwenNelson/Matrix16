#pragma once

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, uint16_t n);
uint16_t strnlen(const char *s, uint16_t maxlen);
char *strchr(const char *s, int c);
char *strcpy(char *dest, const char *src);

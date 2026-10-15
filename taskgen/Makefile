CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: taskgen

taskgen: taskgen.c ../kernel/src/mtsk16.h ../common/libc/src/lzss.c ../common/libc/include/lzss.h
	$(CC) $(CFLAGS) -iquote ../kernel/src -iquote ../common/libc/include -o $@ taskgen.c ../common/libc/src/lzss.c

clean:
	rm -f taskgen

.PHONY: all clean

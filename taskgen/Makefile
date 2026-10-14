CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: taskgen

taskgen: taskgen.c ../kernel/src/mtsk16.h
	$(CC) $(CFLAGS) -iquote ../kernel/src -o $@ taskgen.c

clean:
	rm -f taskgen

.PHONY: all clean

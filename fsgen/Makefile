CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: fsgen

fsgen: fsgen.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f fsgen

.PHONY: all clean

CC = ia16-elf-gcc
AR = ia16-elf-ar

CFLAGS = -march=i8088 -Os -ffreestanding -ffixed-ds -ffixed-es -ffunction-sections -fdata-sections -Iinclude -I../../include -I../../common/libc/include

SRC = src
BUILD = build
LIBRARY = $(BUILD)/libm16c-user.a

all: $(LIBRARY)

$(BUILD):
	mkdir -p $@

$(BUILD)/printf.o: $(SRC)/printf.c include/stdio.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/syscalls.o: $(SRC)/syscalls.c include/syscalls.h ../../include/syscalls.inc | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(BUILD)/printf.o $(BUILD)/syscalls.o
	$(AR) rcs $@ $^

clean:
	rm -f $(BUILD)/printf.o $(BUILD)/syscalls.o $(LIBRARY)

.PHONY: all clean

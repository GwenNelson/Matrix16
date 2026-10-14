NASM    = nasm
CC      = ia16-elf-gcc
LD      = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy

CFLAGS  = -march=i8088 -Os -ffreestanding -ffixed-ds -ffixed-es -ffunction-sections -fdata-sections -I../../include -I../../common/libc/include -I../libc/include
LDFLAGS = --gc-sections

SRC   = src
BUILD = build
COMMON_LIBC = ../../common/libc/build/libm16c-common.a
COMMON_LIBC_INPUTS = ../../common/libc/Makefile ../../common/libc/src/string.c ../../common/libc/include/string.h
USER_LIBC = ../libc/build/libm16c-user.a
USER_LIBC_INPUTS = ../libc/Makefile ../libc/src/printf.c ../libc/src/syscalls.c ../libc/include/stdio.h ../libc/include/syscalls.h ../../include/syscalls.inc

all: $(BUILD)/SHELL.BIN

$(BUILD):
	mkdir -p $@

$(BUILD)/entry.o: $(SRC)/entry.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/shell_vectors.o: $(SRC)/shell_vectors.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/shell.o: $(SRC)/shell.c ../libc/include/syscalls.h ../../include/syscalls.inc | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(COMMON_LIBC): $(COMMON_LIBC_INPUTS)
	$(MAKE) -C ../../common/libc/

$(USER_LIBC): $(USER_LIBC_INPUTS)
	$(MAKE) -C ../libc/

$(BUILD)/shell.elf: $(BUILD)/shell_vectors.o $(BUILD)/entry.o $(BUILD)/shell.o $(USER_LIBC) $(COMMON_LIBC) $(SRC)/linker.ld
	$(LD) $(LDFLAGS) -Map $(BUILD)/shell.map -T $(SRC)/linker.ld -o $@ $(BUILD)/shell_vectors.o $(BUILD)/entry.o $(BUILD)/shell.o $(USER_LIBC) $(COMMON_LIBC)

$(BUILD)/SHELL.BIN: $(BUILD)/shell.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(BUILD)/shell_vectors.o $(BUILD)/entry.o $(BUILD)/shell.o $(BUILD)/shell.elf $(BUILD)/shell.map $(BUILD)/SHELL.BIN

.PHONY: all clean

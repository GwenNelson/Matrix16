NASM = nasm
CC = ia16-elf-gcc
LD = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy
CMD_MAX_SIZE = 28672
CFLAGS = -march=i8088 -Os -ffreestanding -ffixed-ds -ffixed-es -ffunction-sections -fdata-sections -I../../include -I../../common/libc/include -I../libc/include
LDFLAGS = --gc-sections

.DELETE_ON_ERROR:

SRC = src
BUILD = build
COMMON_LIBC = ../../common/libc/build/libm16c-common.a
COMMON_LIBC_INPUTS = ../../common/libc/Makefile ../../common/libc/src/string.c ../../common/libc/include/string.h
USER_LIBC = ../libc/build/libm16c-user.a
USER_LIBC_INPUTS = ../libc/Makefile ../libc/src/printf.c ../libc/src/syscalls.c ../libc/include/stdio.h ../libc/include/syscalls.h ../../include/syscalls.inc

all: $(BUILD)/CLS.CMD $(BUILD)/TYPE.CMD $(BUILD)/DIR.CMD $(BUILD)/SET.CMD $(BUILD)/HELP.CMD $(BUILD)/FORMAT.CMD $(BUILD)/CREATEFILE.CMD $(BUILD)/COPY.CMD $(BUILD)/DEL.CMD $(BUILD)/MORE.CMD

$(BUILD):
	mkdir -p $@

$(BUILD)/CLS.CMD: $(SRC)/cls.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/TYPE.CMD: $(SRC)/type.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/DIR.CMD: $(SRC)/dir.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/SET.CMD: $(SRC)/set.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/HELP.CMD: $(SRC)/help.asm $(SRC)/help.txt | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/format_entry.o: $(SRC)/format_entry.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/createfile_entry.o: $(SRC)/createfile_entry.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/format.o: $(SRC)/format.c ../libc/include/stdio.h ../libc/include/syscalls.h ../../include/syscalls.inc | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/createfile.o: $(SRC)/createfile.c ../libc/include/stdio.h ../libc/include/syscalls.h ../../include/syscalls.inc | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(COMMON_LIBC): $(COMMON_LIBC_INPUTS)
	$(MAKE) -C ../../common/libc/

$(USER_LIBC): $(USER_LIBC_INPUTS)
	$(MAKE) -C ../libc/

$(BUILD)/format.elf: $(BUILD)/format_entry.o $(BUILD)/format.o $(USER_LIBC) $(COMMON_LIBC) $(SRC)/format.ld
	$(LD) $(LDFLAGS) -Map $(BUILD)/format.map -T $(SRC)/format.ld -o $@ $(filter-out $(SRC)/format.ld,$^)

$(BUILD)/createfile.elf: $(BUILD)/createfile_entry.o $(BUILD)/createfile.o $(USER_LIBC) $(COMMON_LIBC) $(SRC)/createfile.ld
	$(LD) $(LDFLAGS) -Map $(BUILD)/createfile.map -T $(SRC)/createfile.ld -o $@ $(filter-out $(SRC)/createfile.ld,$^)

$(BUILD)/FORMAT.CMD: $(BUILD)/format.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/CREATEFILE.CMD: $(BUILD)/createfile.elf
	$(OBJCOPY) -O binary $< $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/DEL.CMD: $(SRC)/del.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/MORE.CMD: $(SRC)/more.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

$(BUILD)/COPY.CMD: $(SRC)/copy.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

clean:
	rm -f $(BUILD)/CLS.CMD $(BUILD)/TYPE.CMD $(BUILD)/DIR.CMD $(BUILD)/SET.CMD $(BUILD)/HELP.CMD $(BUILD)/format_entry.o $(BUILD)/format.o $(BUILD)/format.elf $(BUILD)/format.map $(BUILD)/FORMAT.CMD $(BUILD)/createfile_entry.o $(BUILD)/createfile.o $(BUILD)/createfile.elf $(BUILD)/createfile.map $(BUILD)/CREATEFILE.CMD $(BUILD)/COPY.CMD $(BUILD)/DEL.CMD $(BUILD)/MORE.CMD

.PHONY: all clean

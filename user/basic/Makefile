NASM = nasm
CC = ia16-elf-gcc
LD = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy

CFLAGS = -march=i8088 -Os -ffreestanding -ffixed-ds -ffixed-es -ffunction-sections -fdata-sections -I../../include -I../../common/libc/include -I../../user/libc/include
LDFLAGS = --gc-sections

SRC = src
BUILD = build
COMMON_LIBC = ../../common/libc/build/libm16c-common.a
USER_LIBC = ../../user/libc/build/libm16c-user.a

all: $(BUILD)/BASIC.TSK

$(BUILD):
	mkdir -p $@

$(BUILD)/entry.o: $(SRC)/entry.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/basic.o: $(SRC)/basic.c ../../include/syscalls.inc ../../user/libc/include/syscalls.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(COMMON_LIBC):
	$(MAKE) -C ../../common/libc/

$(USER_LIBC):
	$(MAKE) -C ../../user/libc/

../../taskgen/taskgen:
	$(MAKE) -C ../../taskgen/

$(BUILD)/basic.elf: $(BUILD)/entry.o $(BUILD)/basic.o $(COMMON_LIBC) $(USER_LIBC) $(SRC)/linker.ld
	$(LD) $(LDFLAGS) -Map $(BUILD)/basic.map -T $(SRC)/linker.ld -o $@ $(BUILD)/entry.o $(BUILD)/basic.o $(USER_LIBC) $(COMMON_LIBC)

$(BUILD)/BASIC.BIN: $(BUILD)/basic.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/BASIC.TSK: $(BUILD)/BASIC.BIN ../../taskgen/taskgen
	../../taskgen/taskgen $< $@

clean:
	rm -f $(BUILD)/entry.o $(BUILD)/basic.o $(BUILD)/basic.elf $(BUILD)/basic.map $(BUILD)/BASIC.BIN $(BUILD)/BASIC.TSK

.PHONY: all clean

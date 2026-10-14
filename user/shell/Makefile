NASM    = nasm
CC      = ia16-elf-gcc
LD      = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy

CFLAGS  = -march=i8088 -Os -ffreestanding -ffixed-ds -ffixed-es -ffunction-sections -fdata-sections -I../../include
LDFLAGS = --gc-sections

SRC   = src
BUILD = build

all: $(BUILD)/SHELL.BIN

$(BUILD):
	mkdir -p $@

$(BUILD)/entry.o: $(SRC)/entry.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/shell.o: $(SRC)/shell.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/shell.elf: $(BUILD)/entry.o $(BUILD)/shell.o $(SRC)/linker.ld
	$(LD) $(LDFLAGS) -T $(SRC)/linker.ld -o $@ $(BUILD)/entry.o $(BUILD)/shell.o

$(BUILD)/SHELL.BIN: $(BUILD)/shell.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(BUILD)/entry.o $(BUILD)/shell.o $(BUILD)/shell.elf $(BUILD)/SHELL.BIN

.PHONY: all clean

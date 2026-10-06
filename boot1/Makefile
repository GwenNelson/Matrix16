NASM    = nasm
CC      = ia16-elf-gcc
LD      = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy

SRC     = src
BUILD   = build

all: $(BUILD)/matrix16.img

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kentry.o: $(SRC)/kentry.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/biosdisk.o: $(SRC)/biosdisk.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/isr.o: $(SRC)/isr.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/kmain.o: $(SRC)/kmain.c | $(BUILD)
	$(CC) -ffreestanding -c $(SRC)/kmain.c -o $@

$(BUILD)/kernel.elf: $(BUILD)/kentry.o $(BUILD)/isr.o $(BUILD)/biosdisk.o $(BUILD)/kmain.o
	$(LD) -T $(SRC)/kernel.ld -o $@ $^

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/matrix16.img: $(SRC)/boot1.asm $(BUILD)/kernel.bin
	$(NASM) -f bin $(SRC)/boot1.asm -o $@

run: $(BUILD)/matrix16.img
	qemu-system-i386 -drive file=$<,format=raw,if=floppy

clean:
	rm -rf $(BUILD)

.PHONY: all run clean

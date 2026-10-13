NASM    = nasm
CC      = ia16-elf-gcc
LD      = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy

SRC     = src
BUILD   = build

all: $(BUILD)/kernel.bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kentry.o: $(SRC)/kentry.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/biosdisk.o: $(SRC)/biosdisk.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/isr.o: $(SRC)/isr.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/switchto.o: $(SRC)/switchto.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/kmain.o: $(SRC)/kmain.c $(SRC)/bioscons.h $(SRC)/kpanic.h $(SRC)/utils.h | $(BUILD)
	$(CC) -march=i8088 -ffreestanding -c $(SRC)/kmain.c -o $@

$(BUILD)/bioscons.o: $(SRC)/bioscons.c $(SRC)/bioscons.h | $(BUILD)
	$(CC) -march=i8088 -ffreestanding -c $(SRC)/bioscons.c -o $@

$(BUILD)/kpanic.o: $(SRC)/kpanic.c $(SRC)/kpanic.h $(SRC)/bioscons.h | $(BUILD)
	$(CC) -march=i8088 -ffreestanding -c $(SRC)/kpanic.c -o $@

$(BUILD)/utils.o: $(SRC)/utils.c $(SRC)/utils.h | $(BUILD)
	$(CC) -march=i8088 -ffreestanding -c $(SRC)/utils.c -o $@

$(BUILD)/kconsole.o: $(SRC)/kconsole.c $(SRC)/kconsole.h | $(BUILD)
	$(CC) -march=i8088 -Os -ffreestanding -c $(SRC)/kconsole.c -o $@

$(BUILD)/kthread.o: $(SRC)/kthread.c $(SRC)/kthread.h | $(BUILD)
	$(CC) -march=i8088 -Os -ffreestanding -c $(SRC)/kthread.c -o $@

$(BUILD)/kernel.elf: $(BUILD)/kentry.o $(BUILD)/isr.o $(BUILD)/switchto.o $(BUILD)/biosdisk.o $(BUILD)/kmain.o $(BUILD)/bioscons.o $(BUILD)/kpanic.o $(BUILD)/utils.o $(BUILD)/kconsole.o $(BUILD)/kthread.o
	$(LD) -T $(SRC)/kernel.ld -o $@ $^

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean

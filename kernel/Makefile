NASM    = nasm
CC      = ia16-elf-gcc
LD      = ia16-elf-ld
CFLAGS  = -march=i8088 -Os -ffreestanding -ffixed-es -ffunction-sections -fdata-sections -I../include -I../common/libc/include
LDFLAGS = --gc-sections
OBJCOPY = ia16-elf-objcopy

SRC     = src
BUILD   = build
COMMON_LIBC = ../common/libc/build/libm16c-common.a
COMMON_LIBC_INPUTS = ../common/libc/Makefile ../common/libc/src/string.c ../common/libc/src/lzss.c ../common/libc/include/string.h ../common/libc/include/lzss.h

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

$(BUILD)/waitkey.o: $(SRC)/waitkey.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/kmain.o: $(SRC)/kmain.c $(SRC)/bioscons.h $(SRC)/biosdisk.h $(SRC)/isr.h $(SRC)/kfiles.h $(SRC)/kpanic.h $(SRC)/matfs16.h $(SRC)/mtsk16.h $(SRC)/parsecfg.h ../common/libc/include/string.h $(SRC)/utils.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/kmain.c -o $@

$(BUILD)/parsecfg.o: $(SRC)/parsecfg.c $(SRC)/parsecfg.h $(SRC)/bioscons.h $(SRC)/kfiles.h $(SRC)/matfs16.h ../common/libc/include/string.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/parsecfg.c -o $@

$(BUILD)/bioscons.o: $(SRC)/bioscons.c $(SRC)/bioscons.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/bioscons.c -o $@

$(BUILD)/kpanic.o: $(SRC)/kpanic.c $(SRC)/kpanic.h $(SRC)/bioscons.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/kpanic.c -o $@

$(BUILD)/kheap.o: $(SRC)/kheap.c $(SRC)/kheap.h $(SRC)/kpanic.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/kheap.c -o $@

$(BUILD)/kfiles.o: $(SRC)/kfiles.c $(SRC)/kfiles.h $(SRC)/kheap.h $(SRC)/kmemmap.h $(SRC)/kthread.h $(SRC)/matfs16.h $(SRC)/utils.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/kfiles.c -o $@

$(BUILD)/ksyscall.o: $(SRC)/ksyscall.c $(SRC)/ksyscall.h $(SRC)/8086.h $(SRC)/bioscons.h $(SRC)/kfiles.h $(SRC)/kpanic.h $(SRC)/matfs16.h $(SRC)/utils.h ../include/syscalls.inc | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/ksyscall.c -o $@

$(BUILD)/utils.o: $(SRC)/utils.c $(SRC)/utils.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/utils.c -o $@

$(BUILD)/matfs16.o: $(SRC)/matfs16.c $(SRC)/matfs16.h $(SRC)/biosdisk.h $(SRC)/kmemmap.h $(SRC)/kthread.h $(SRC)/utils.h ../common/libc/include/string.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/matfs16.c -o $@

$(BUILD)/task.o: $(SRC)/task.c $(SRC)/task.h $(SRC)/mtsk16.h $(SRC)/matfs16.h $(SRC)/bioscons.h $(SRC)/kmemmap.h ../common/libc/include/lzss.h $(SRC)/utils.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/task.c -o $@

$(BUILD)/kconsole.o: $(SRC)/kconsole.c $(SRC)/kconsole.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/kconsole.c -o $@

$(BUILD)/kthread.o: $(SRC)/kthread.c $(SRC)/kthread.h $(SRC)/utils.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/kthread.c -o $@

$(BUILD)/savemenu.o: $(SRC)/savemenu.c $(SRC)/savemenu.h $(SRC)/kthread.h $(SRC)/kconsole.h $(SRC)/mtsk16.h $(SRC)/matfs16.h $(SRC)/task.h $(SRC)/kmemmap.h $(SRC)/kfiles.h $(SRC)/utils.h | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/savemenu.c -o $@

$(COMMON_LIBC): $(COMMON_LIBC_INPUTS)
	$(MAKE) -C ../common/libc/

$(BUILD)/kernel.elf: $(BUILD)/kentry.o $(BUILD)/isr.o $(BUILD)/switchto.o $(BUILD)/waitkey.o $(BUILD)/biosdisk.o $(BUILD)/kmain.o $(BUILD)/parsecfg.o $(BUILD)/bioscons.o $(BUILD)/kpanic.o $(BUILD)/kheap.o $(BUILD)/kfiles.o $(BUILD)/ksyscall.o $(BUILD)/utils.o $(BUILD)/matfs16.o $(BUILD)/task.o $(BUILD)/kconsole.o $(BUILD)/kthread.o $(BUILD)/savemenu.o $(COMMON_LIBC) $(SRC)/kernel.ld
	$(LD) $(LDFLAGS) -T $(SRC)/kernel.ld -o $@ $(filter-out $(SRC)/kernel.ld,$^)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean

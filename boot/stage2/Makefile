NASM    = nasm
CC      = ia16-elf-gcc
LD      = ia16-elf-ld
OBJCOPY = ia16-elf-objcopy

SRC   = src
BUILD = build

all: $(BUILD)/stage2.bin

$(BUILD):
	mkdir -p $@

$(BUILD)/entry.o: $(SRC)/entry.asm | $(BUILD)
	$(NASM) -f elf $< -o $@

$(BUILD)/stage2.o: $(SRC)/stage2.c | $(BUILD)
	$(CC) -march=i8088 -Os -ffreestanding -c $< -o $@

# The existing linker script places code at offset zero and supplies the
# __bss_start/__bss_end symbols used by the unchanged entry assembly.
$(BUILD)/stage2.elf: $(BUILD)/entry.o $(BUILD)/stage2.o ../../boot1/src/kernel.ld
	$(LD) -T ../../boot1/src/kernel.ld -o $@ $(BUILD)/entry.o $(BUILD)/stage2.o

$(BUILD)/stage2.bin: $(BUILD)/stage2.elf
	$(OBJCOPY) -O binary $< $@
	@bytes=$$(wc -c < $@); \
	if test "$$bytes" -gt 4096; then \
		echo "stage 2 exceeds eight sectors ($$bytes bytes)" >&2; \
		rm -f $@; exit 1; \
	fi

clean:
	rm -f $(BUILD)/entry.o $(BUILD)/stage2.o $(BUILD)/stage2.elf $(BUILD)/stage2.bin

.PHONY: all clean

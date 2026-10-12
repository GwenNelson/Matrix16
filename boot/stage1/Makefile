NASM  = nasm
BUILD = build
SRC   = src

all: $(BUILD)/bootsector.bin

# The unchanged source embeds build/kernel.bin and emits a whole legacy floppy.
# Assemble it from boot1/ so its existing incbin path resolves, then take LBA 0.
../../boot1/build/kernel.bin: FORCE
	$(MAKE) -C ../../boot1 build/kernel.bin

$(BUILD)/bootsector.bin: $(SRC)/boot1.asm ../../boot1/build/kernel.bin | $(BUILD)
	cd ../../boot1 && $(NASM) -f bin ../boot/stage1/src/boot1.asm -o ../boot/stage1/$(BUILD)/legacy.img
	dd if=$(BUILD)/legacy.img of=$@ bs=512 count=1 status=none
	test "$$(wc -c < $@)" -eq 512

$(BUILD):
	mkdir -p $@

clean:
	rm -f $(BUILD)/bootsector.bin $(BUILD)/legacy.img

.PHONY: all clean FORCE

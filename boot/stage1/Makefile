NASM  = nasm
BUILD = build
SRC   = src

all: $(BUILD)/bootsector.bin

$(BUILD)/bootsector.bin: $(SRC)/boot1.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	test "$$(wc -c < $@)" -eq 512

$(BUILD):
	mkdir -p $@

clean:
	rm -f $(BUILD)/bootsector.bin

.PHONY: all clean

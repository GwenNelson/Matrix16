NASM = nasm
CMD_MAX_SIZE = 28672

.DELETE_ON_ERROR:

SRC = src
BUILD = build

all: $(BUILD)/CLS.CMD

$(BUILD):
	mkdir -p $@

$(BUILD)/CLS.CMD: $(SRC)/cls.asm | $(BUILD)
	$(NASM) -f bin $< -o $@
	@test "$$(wc -c < $@)" -le $(CMD_MAX_SIZE) || \
		{ echo "$@ exceeds the 8000h-EFFFh command region"; false; }

clean:
	rm -f $(BUILD)/CLS.CMD

.PHONY: all clean

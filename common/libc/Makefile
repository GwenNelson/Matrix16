CC = ia16-elf-gcc
AR = ia16-elf-ar

CFLAGS = -march=i8088 -Os -ffreestanding -ffixed-ds -ffixed-es -ffunction-sections -fdata-sections -Iinclude

SRC = src
BUILD = build
LIBRARY = $(BUILD)/libm16c-common.a

all: $(LIBRARY)

$(BUILD):
	mkdir -p $@

$(BUILD)/string.o: $(SRC)/string.c include/string.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(BUILD)/string.o
	$(AR) rcs $@ $^

clean:
	rm -f $(BUILD)/string.o $(LIBRARY)

.PHONY: all clean

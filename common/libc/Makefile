CC = ia16-elf-gcc
AR = ia16-elf-ar
HOST_CC ?= cc

CFLAGS = -march=i8088 -Os -ffreestanding -ffixed-es -ffunction-sections -fdata-sections -Iinclude
HOST_CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -Iinclude

SRC = src
BUILD = build
LIBRARY = $(BUILD)/libm16c-common.a

all: $(LIBRARY)

$(BUILD):
	mkdir -p $@

$(BUILD)/string.o: $(SRC)/string.c include/string.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/lzss.o: $(SRC)/lzss.c include/lzss.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/lzss-host.o: $(SRC)/lzss.c include/lzss.h | $(BUILD)
	$(HOST_CC) $(HOST_CFLAGS) -c $< -o $@

$(LIBRARY): $(BUILD)/string.o $(BUILD)/lzss.o
	$(AR) rcs $@ $^

clean:
	rm -f $(BUILD)/string.o $(BUILD)/lzss.o $(BUILD)/lzss-host.o $(LIBRARY)

host-codec: $(BUILD)/lzss-host.o

.PHONY: all clean host-codec

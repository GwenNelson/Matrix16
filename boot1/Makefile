NASM = nasm

all: matrix16.img

kernel.bin: kernel.asm
	$(NASM) -f bin kernel.asm -o kernel.bin

matrix16.img: boot1.asm kernel.bin
	$(NASM) -f bin boot1.asm -o matrix16.img

run: matrix16.img
	qemu-system-i386 -drive file=matrix16.img,format=raw,if=floppy

clean:
	rm -f kernel.bin matrix16.img

.PHONY: all run clean

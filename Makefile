.PHONY: all clean boot1 boot1-clean fsgen fsgen-clean run

all:	boot1 rootfs.img

boot1:
	$(MAKE) -C boot1/

boot1-clean:
	$(MAKE) -C boot1/ clean

fsgen:
	$(MAKE) -C fsgen/

fsgen-clean:
	$(MAKE) -C fsgen/ clean

rootfs/SHELL.PRG: user/src/shell.asm
	mkdir -p rootfs
	nasm -f bin $< -o $@

rootfs.img: fsgen rootfs/SHELL.PRG
	./fsgen/fsgen rootfs.img rootfs/

run: boot1 rootfs.img
	qemu-system-i386 \
		-drive file=boot1/build/matrix16.img,format=raw,if=floppy,index=0 \
		-drive file=rootfs.img,format=raw,if=floppy,index=1

clean: boot1-clean fsgen-clean
	rm -f rootfs/SHELL.PRG

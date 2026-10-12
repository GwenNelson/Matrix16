.PHONY: all clean boot1 boot1-clean stage1 stage1-clean stage2 stage2-clean fsgen fsgen-clean run

all: rootfs.img boot1

boot1:
	$(MAKE) -C boot1/

boot1-clean:
	$(MAKE) -C boot1/ clean

stage1:
	$(MAKE) -C boot/stage1/

stage1-clean:
	$(MAKE) -C boot/stage1/ clean

stage2:
	$(MAKE) -C boot/stage2/

stage2-clean:
	$(MAKE) -C boot/stage2/ clean

fsgen:
	$(MAKE) -C fsgen/

fsgen-clean:
	$(MAKE) -C fsgen/ clean

rootfs/SHELL.PRG: user/src/shell.asm
	mkdir -p rootfs
	nasm -f bin $< -o $@

boot/stage1/build/bootsector.bin: stage1

boot/stage2/build/stage2.bin: stage2

boot1/build/kernel.bin:
	$(MAKE) -C boot1/ build/kernel.bin

rootfs.img: boot/stage1/build/bootsector.bin boot/stage2/build/stage2.bin boot1/build/kernel.bin fsgen rootfs/SHELL.PRG rootfs/README.TXT rootfs/SYSTEM.CFG
	./fsgen/fsgen rootfs.img \
		--bootsector boot/stage1/build/bootsector.bin \
		--stage2 boot/stage2/build/stage2.bin \
		--kernel boot1/build/kernel.bin \
		--file SHELL.PRG=rootfs/SHELL.PRG \
		--file README.TXT=rootfs/README.TXT \
		--file SYSTEM.CFG=rootfs/SYSTEM.CFG

run: rootfs.img boot1
	qemu-system-i386 \
		-drive file=rootfs.img,format=raw,if=floppy,index=0 \
		-drive file=boot1/build/matrix16.img,format=raw,if=floppy,index=1

clean: stage1-clean stage2-clean boot1-clean fsgen-clean
	rm -f rootfs/SHELL.PRG rootfs.img

.PHONY: all clean kernel kernel-clean stage1 stage1-clean stage2 stage2-clean fsgen fsgen-clean taskgen taskgen-clean shell shell-clean run

all: rootfs.img kernel

kernel:
	$(MAKE) -C kernel/

kernel-clean:
	$(MAKE) -C kernel/ clean

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

taskgen:
	$(MAKE) -C taskgen/

taskgen-clean:
	$(MAKE) -C taskgen/ clean

shell:
	$(MAKE) -C user/shell/

shell-clean:
	$(MAKE) -C user/shell/ clean

user/shell/build/SHELL.BIN: shell

rootfs/SHELL.TSK: user/shell/build/SHELL.BIN taskgen
	mkdir -p rootfs
	./taskgen/taskgen $< $@

user/build/HELLO.BIN: user/src/hello.asm
	mkdir -p user/build
	nasm -f bin $< -o $@

rootfs/HELLO.TSK: user/build/HELLO.BIN taskgen
	mkdir -p rootfs
	./taskgen/taskgen $< $@

boot/stage1/build/bootsector.bin: stage1

boot/stage2/build/stage2.bin: stage2

kernel/build/kernel.bin: kernel

rootfs.img: boot/stage1/build/bootsector.bin boot/stage2/build/stage2.bin kernel/build/kernel.bin fsgen rootfs/SHELL.TSK rootfs/HELLO.TSK rootfs/README.TXT rootfs/SYSTEM.CFG
	./fsgen/fsgen rootfs.img \
		--bootsector boot/stage1/build/bootsector.bin \
		--stage2 boot/stage2/build/stage2.bin \
		--kernel kernel/build/kernel.bin \
		--task0 HELLO.TSK=rootfs/HELLO.TSK \
		--file SHELL.TSK=rootfs/SHELL.TSK \
		--file README.TXT=rootfs/README.TXT \
		--file SYSTEM.CFG=rootfs/SYSTEM.CFG

run: rootfs.img kernel
	qemu-system-i386 \
		-drive file=rootfs.img,format=raw,if=floppy,index=0

clean: stage1-clean stage2-clean kernel-clean fsgen-clean taskgen-clean shell-clean
	rm -f user/build/SHELL.BIN user/build/HELLO.BIN rootfs/SHELL.TSK rootfs/HELLO.TSK rootfs.img

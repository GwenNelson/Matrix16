.PHONY: all clean kernel kernel-clean stage1 stage1-clean stage2 stage2-clean fsgen fsgen-clean taskgen taskgen-clean common-libc common-libc-clean user-libc user-libc-clean shell shell-clean cmds cmds-clean basic basic-clean run

all: rootfs.img kernel

kernel: common-libc
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

common-libc:
	$(MAKE) -C common/libc/

common-libc-clean:
	$(MAKE) -C common/libc/ clean

user-libc:
	$(MAKE) -C user/libc/

user-libc-clean:
	$(MAKE) -C user/libc/ clean

shell: common-libc user-libc
	$(MAKE) -C user/shell/

shell-clean:
	$(MAKE) -C user/shell/ clean

user/shell/build/SHELL.BIN: shell

cmds:
	$(MAKE) -C user/cmd/

cmds-clean:
	$(MAKE) -C user/cmd/ clean

user/cmd/build/CLS.CMD: cmds

user/cmd/build/TYPE.CMD: cmds

user/cmd/build/DIR.CMD: cmds

user/cmd/build/SET.CMD: cmds

user/cmd/build/HELP.CMD: cmds

user/cmd/build/FORMAT.CMD: cmds

user/cmd/build/CREATEFILE.CMD: cmds

user/cmd/build/DEL.CMD: cmds

user/cmd/build/MORE.CMD: cmds

user/cmd/build/COPY.CMD: cmds

user/cmd/build/COPYSYS.CMD: cmds

user/cmd/build/REN.CMD: cmds

user/cmd/build/DISKUSAGE.CMD: cmds

user/cmd/build/TERMINATE.CMD: cmds

user/cmd/build/EXEC.CMD: cmds

user/cmd/build/EDIT.CMD: cmds

user/cmd/build/ASM.CMD: cmds

user/cmd/build/LINK.CMD: cmds

basic: taskgen
	$(MAKE) -C user/basic/

basic-clean:
	$(MAKE) -C user/basic/ clean

user/basic/build/BASIC.TSK: basic

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

rootfs.img: boot/stage1/build/bootsector.bin boot/stage2/build/stage2.bin kernel/build/kernel.bin fsgen rootfs/SHELL.TSK rootfs/HELLO.TSK user/basic/build/BASIC.TSK user/cmd/build/CLS.CMD user/cmd/build/TYPE.CMD user/cmd/build/DIR.CMD user/cmd/build/SET.CMD user/cmd/build/HELP.CMD user/cmd/build/FORMAT.CMD user/cmd/build/CREATEFILE.CMD user/cmd/build/COPY.CMD user/cmd/build/COPYSYS.CMD user/cmd/build/DEL.CMD user/cmd/build/MORE.CMD user/cmd/build/REN.CMD user/cmd/build/DISKUSAGE.CMD user/cmd/build/TERMINATE.CMD user/cmd/build/EXEC.CMD user/cmd/build/EDIT.CMD user/cmd/build/ASM.CMD user/cmd/build/LINK.CMD rootfs/ASMGUIDE.TXT rootfs/README.TXT rootfs/SYSTEM.CFG rootfs/testcmd.asm rootfs/testtsk.asm
	./fsgen/fsgen rootfs.img \
		--bootsector boot/stage1/build/bootsector.bin \
		--stage2 boot/stage2/build/stage2.bin \
		--kernel kernel/build/kernel.bin \
		--task0 HELLO.TSK=rootfs/HELLO.TSK \
		--task1 BASIC.TSK=user/basic/build/BASIC.TSK \
		--file SHELL.TSK=rootfs/SHELL.TSK \
		--file CLS.CMD=user/cmd/build/CLS.CMD \
		--file TYPE.CMD=user/cmd/build/TYPE.CMD \
		--file DIR.CMD=user/cmd/build/DIR.CMD \
		--file SET.CMD=user/cmd/build/SET.CMD \
		--file HELP.CMD=user/cmd/build/HELP.CMD \
		--file FORMAT.CMD=user/cmd/build/FORMAT.CMD \
		--file CREATEFILE.CMD=user/cmd/build/CREATEFILE.CMD \
		--file COPY.CMD=user/cmd/build/COPY.CMD \
		--file COPYSYS.CMD=user/cmd/build/COPYSYS.CMD \
		--file DEL.CMD=user/cmd/build/DEL.CMD \
		--file MORE.CMD=user/cmd/build/MORE.CMD \
		--file REN.CMD=user/cmd/build/REN.CMD \
		--file DISKUSAGE.CMD=user/cmd/build/DISKUSAGE.CMD \
		--file TERMINATE.CMD=user/cmd/build/TERMINATE.CMD \
		--file EXEC.CMD=user/cmd/build/EXEC.CMD \
		--file EDIT.CMD=user/cmd/build/EDIT.CMD \
		--file ASM.CMD=user/cmd/build/ASM.CMD \
		--file LINK.CMD=user/cmd/build/LINK.CMD \
		--file ASMGUIDE.TXT=rootfs/ASMGUIDE.TXT \
		--file README.TXT=rootfs/README.TXT \
		--file TESTCMD.ASM=rootfs/testcmd.asm \
		--file TESTTSK.ASM=rootfs/testtsk.asm \
		--file SYSTEM.CFG=rootfs/SYSTEM.CFG


run: rootfs.img kernel
	qemu-system-i386 \
		-monitor stdio \
		-drive file=rootfs.img,format=raw,if=floppy,index=0


clean: stage1-clean stage2-clean kernel-clean fsgen-clean taskgen-clean common-libc-clean user-libc-clean shell-clean cmds-clean basic-clean
	rm -f user/build/SHELL.BIN user/build/HELLO.BIN rootfs/SHELL.TSK rootfs/HELLO.TSK rootfs.img

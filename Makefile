.PHONY: boot1 boot1-clean fsgen fsgen-clean run

boot1:
	$(MAKE) -C boot1/

boot1-clean:
	$(MAKE) -C boot1/ clean

fsgen:
	$(MAKE) -C fsgen/

fsgen-clean:
	$(MAKE) -C fsgen/ clean

rootfs.img: fsgen
	./fsgen/fsgen rootfs.img rootfs/

run: boot1 rootfs.img
	qemu-system-i386 \
		-drive file=boot1/build/matrix16.img,format=raw,if=floppy,index=0 \
		-drive file=rootfs.img,format=raw,if=floppy,index=1

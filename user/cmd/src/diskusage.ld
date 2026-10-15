OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(diskusage_entry)

SECTIONS
{
	. = 0x8000;

	.text : {
		KEEP(*(.text.entry))
		*(.text)
		*(.text.*)
	}

	.rodata : {
		*(.rodata)
		*(.rodata.*)
	}

	.data : {
		*(.data)
		*(.data.*)
	}

	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		__bss_end = .;
	}

	__diskusage_end = .;

	ASSERT(diskusage_entry == 0x8000,
	       "DISKUSAGE.CMD entry must begin at offset 8000h")
	ASSERT(__diskusage_end <= 0xF000,
	       "DISKUSAGE.CMD extends beyond the command region")
}

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(createfile_entry)

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

	__createfile_end = .;

	ASSERT(createfile_entry == 0x8000,
	       "CREATEFILE.CMD entry must begin at offset 8000h")
	ASSERT(__createfile_end <= 0xF000,
	       "CREATEFILE.CMD extends beyond the command region")
}

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(format_entry)

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

	__format_end = .;

	ASSERT(format_entry == 0x8000,
	       "FORMAT.CMD entry must begin at offset 8000h")
	ASSERT(__format_end <= 0xF000,
	       "FORMAT.CMD extends beyond the command region")
}

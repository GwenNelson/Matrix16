OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(copysys_entry)

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

	__copysys_end = .;

	ASSERT(copysys_entry == 0x8000,
	       "COPYSYS.CMD entry must begin at offset 8000h")
	ASSERT(__copysys_end <= 0xF000,
	       "COPYSYS.CMD extends beyond the command region")
}

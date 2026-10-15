OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(edit_entry)

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

	__edit_end = .;

	ASSERT(edit_entry == 0x8000,
	       "EDIT.CMD entry must begin at offset 8000h")
	ASSERT(__edit_end <= 0xF000,
	       "EDIT.CMD and its static buffer extend beyond the command region")
}
